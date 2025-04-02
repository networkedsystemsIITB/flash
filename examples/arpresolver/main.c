#include <flash_nf.h>
#include <flash_params.h>
#include <flash_uds.h>

#include <signal.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>
#include <log.h>
#include <sys/ioctl.h>
#include <net/ethernet.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <net/if.h>

#include <string.h>

#include <stdint.h>

#define MAX_IPS 10
#define IP_STRLEN 16
#define PROTO_STRLEN 4
#define IFNAME_STRLEN 256

bool done = false;
struct config *cfg = NULL;
struct nf *nf;

int num_valid_ips;
char resp_to_ips[MAX_IPS][IP_STRLEN];
uint8_t src_mac[ETH_ALEN];

static int get_mac_address(void)
{
	int fd;
	struct ifreq ifr;

	// Open a socket
	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd == -1) {
		perror("socket");
		return -1;
	}

	// Copy interface name into ifreq structure
	strncpy(ifr.ifr_name, cfg->ifname, IF_NAMESIZE - 1);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	// Perform IOCTL to get MAC address
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
		perror("ioctl");
		close(fd);
		return -1;
	}

	close(fd);

	// Copy MAC address to src_mac array
	memcpy(src_mac, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

	return 0; // Success
}

static void configure(void)
{
	// Need to change so that we get IPS of all NFS, not just of our local dest
	send_cmd(cfg->uds_sockfd, FLASH__GET_DST_IP_ADDR);
	recv_data(cfg->uds_sockfd, &num_valid_ips, sizeof(int));
	if (num_valid_ips > MAX_IPS){
		printf("num_valid_ips > MAX_IPS\n");
		exit(1);
	}
	// log_info("Number of Backends: %d", num_valid_ips);
	for (int i = 0; i < num_valid_ips; i++) {
		recv_data(cfg->uds_sockfd, resp_to_ips[i], INET_ADDRSTRLEN);
		log_info("IP no: %d IP: %s", i, resp_to_ips[i]);
	}

	// configuring src_mac
	if (get_mac_address() < 0){
		printf("Error in ioctl: fetch mac address\n");
		exit(1);
	}
}

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
}

struct appconf {
	int cpu_start;
	int cpu_end;
	int stats_cpu;
} app_conf;

struct Args {
	int socket_id;
	int *next;
	int next_size;
};
// handling IP4
struct __attribute__((packed)) arp_header {
	unsigned short arp_hd;
	unsigned short arp_pr;
	unsigned char arp_hdl;
	unsigned char arp_prl;
	unsigned short arp_op;
	unsigned char arp_sha[6];
	unsigned char arp_spa[4];
	unsigned char arp_dha[6];
	unsigned char arp_dpa[4];
};

static void parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "c:e:s:")) != -1)
		switch (c) {
		case 'c':
			app_conf->cpu_start = atoi(optarg);
			break;
		case 'e':
			app_conf->cpu_end = atoi(optarg);
			break;
		case 's':
			app_conf->stats_cpu = atoi(optarg);
			break;
		default:
			abort();
		}
}

static void *worker__stats(void *arg)
{
	(void)arg;

	if (cfg->verbose) {
		unsigned int interval = cfg->stats_interval;
		setlocale(LC_ALL, "");

		for (int i = 0; i < cfg->total_sockets; i++)
			nf->thread[i]->socket->timestamp = flash__get_nsecs(cfg);

		while (!done) {
			sleep(interval);
			if (system("clear") != 0)
				log_error("Terminal clear error");
			for (int i = 0; i < cfg->total_sockets; i++) {
				flash__dump_stats(cfg, nf->thread[i]->socket);
			}
		}
	}
	return NULL;
}

static void *socket_routine(void *arg)
{
	struct Args *a = (struct Args *)arg;
	int socket_id = a->socket_id;
	int *next = a->next;
	int next_size = a->next_size;
	// free(arg);
	log_info("SOCKET_ID: %d", socket_id);
	// static __u32 nb_frags;
	int i, ret, nfds = 1, nrecv;
	struct pollfd fds[1] = {};
	struct xskmsghdr msg = {};

	log_info("2_NEXT_SIZE: %d", next_size);

	for (int i = 0; i < next_size; i++) {
		log_info("2_NEXT_ITEM_%d %d", i, next[i]);
	}

	msg.msg_iov = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));

	fds[0].fd = nf->thread[socket_id]->socket->fd;
	fds[0].events = POLLIN;

	for (;;) {
		if (cfg->xsk->mode & FLASH__POLL) {
			ret = flash__poll(nf->thread[socket_id]->socket, fds, nfds, cfg->xsk->poll_timeout);
			if (ret <= 0 || ret > 1)
				continue;
		}
		nrecv = flash__recvmsg(cfg, nf->thread[socket_id]->socket, &msg);

		struct xskvec *drop[nrecv];
		unsigned int tot_pkt_drop = 0;
		struct xskvec *send[nrecv];
		unsigned int tot_pkt_send = 0;

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &msg.msg_iov[i];
			void *pkt = xv->data;
			void *pkt_end = pkt + xv->len;

			uint8_t tmp_mac[ETH_ALEN];
			unsigned char buff_ip[4];

			struct ethhdr *eth = pkt;
			if ((void *)(eth + 1) > pkt_end) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}

			struct arp_header *arp = (struct arp_header *)(eth + 1);
			if ((void *)(arp + 1) > pkt_end){
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}

			if (ntohs(eth->h_proto) != ETH_P_ARP || (ntohs(arp->arp_op) != ARPOP_REQUEST))
			{
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}

			memcpy(buff_ip, arp->arp_dpa, 4);
			char query_ip[IP_STRLEN];
			inet_ntop(AF_INET, (struct in_addr *)buff_ip, query_ip, sizeof(query_ip));

			for (int i = 0; i < num_valid_ips; i++){
				if (strcmp(resp_to_ips[i], query_ip) == 0){
					goto send_arp_resp;
				}
			}

			drop[tot_pkt_drop++] = &msg.msg_iov[i];
			continue;
send_arp_resp:
			memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
			memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
			memcpy(eth->h_source, src_mac, ETH_ALEN);

			arp->arp_op = htons(ARPOP_REPLY);

			memcpy(buff_ip, arp->arp_dpa, 4);
			memcpy(arp->arp_dpa, arp->arp_spa, 4);
			memcpy(arp->arp_spa, buff_ip, 4);

			memcpy(arp->arp_dha, arp->arp_sha, ETH_ALEN);
			memcpy(arp->arp_sha, src_mac, ETH_ALEN);

			send[tot_pkt_send++] = &msg.msg_iov[i];
		}

		if (nrecv) {
			size_t ret_send = flash__sendmsg(cfg, nf->thread[socket_id]->socket, send, tot_pkt_send);
			size_t ret_drop = flash__dropmsg(cfg, nf->thread[socket_id]->socket, drop, tot_pkt_drop);
			if (ret_send != tot_pkt_send || ret_drop != tot_pkt_drop) {
				log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

		if (done)
			break;
	}
	free(msg.msg_iov);
	return NULL;
}

int main(int argc, char **argv)
{
	cpu_set_t cpuset;
	cfg = calloc(1, sizeof(struct config));
	if (!cfg) {
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	int n = flash__parse_cmdline_args(argc, argv, cfg);
	parse_app_args(argc, argv, &app_conf, n);
	flash__configure_nf(&nf, cfg);
	flash__populate_fill_ring(nf->thread, cfg->umem->frame_size, cfg->total_sockets, cfg->umem_offset, cfg->umem_scale);

	log_info("Control Plane Setup Done");
	configure();
	// Parse JSON
	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	log_info("STARTING Data Path");

	for (int i = 0; i < cfg->total_sockets; i++) {
		struct Args *args = calloc(1, sizeof(struct Args));
		args->socket_id = i;
		args->next = nf->next;
		args->next_size = nf->next_size;

		log_info("2_NEXT_SIZE: %d", args->next_size);

		for (int i = 0; i < args->next_size; i++) {
			log_info("2_NEXT_ITEM_%d %d", i, nf->next[i]);
		}

		pthread_t socket_thread;
		if (pthread_create(&socket_thread, NULL, socket_routine, args)) {
			log_error("Error creating socket thread");
			exit(EXIT_FAILURE);
		}
		CPU_ZERO(&cpuset);
		CPU_SET((i % (app_conf.cpu_end - app_conf.cpu_start + 1)) + app_conf.cpu_start, &cpuset);
		if (pthread_setaffinity_np(socket_thread, sizeof(cpu_set_t), &cpuset) != 0) {
			log_error("ERROR: Unable to set thread affinity: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}

		pthread_detach(socket_thread);
	}

	pthread_t stats_thread;
	if (pthread_create(&stats_thread, NULL, worker__stats, NULL)) {
		log_error("Error creating statistics thread");
		exit(EXIT_FAILURE);
	}
	CPU_ZERO(&cpuset);
	CPU_SET(app_conf.stats_cpu, &cpuset);
	if (pthread_setaffinity_np(stats_thread, sizeof(cpu_set_t), &cpuset) != 0) {
		log_error("ERROR: Unable to set thread affinity: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	pthread_detach(stats_thread);

	wait_for_cmd(cfg);

	flash__xsk_close(cfg, nf);

	return EXIT_SUCCESS;
}