#include <flash_nf.h>
#include <flash_params.h>
#include <flash_uds.h>

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <log.h>
#include <arpa/inet.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include "hashmap.h"

#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

#define CONFIG_FILE "config.json"

#define IP_STRLEN 16
#define PROTO_STRLEN 4
#define IFNAME_STRLEN 256
#define MAX_VALID_SESSIONS 100

bool done = false;
struct config *cfg = NULL;
struct nf *nf;

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

struct session_id {
	uint32_t saddr;
	uint32_t daddr;
	uint16_t sport;
	uint16_t dport;
	uint8_t proto;
} __attribute__((packed));

struct hashmap valid_sessions_map;
struct session_id valid_sessions[MAX_VALID_SESSIONS];
int num_sessions = 0;

char load_balancer_addr[IP_STRLEN];
unsigned int load_balancer_port = 80;

static void read_json_config(void)
{
	struct in_addr addr;
	inet_aton(load_balancer_addr, &addr);
	FILE *file = fopen(CONFIG_FILE, "r");
	if (!file) {
		perror("Failed to open file");
		return;
	}

	// Get file size
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	rewind(file);

	char *json_data = (char *)malloc(file_size + 1);
	if (!json_data) {
		perror("Memory allocation failed");
		fclose(file);
		return;
	}

	size_t read_size = fread(json_data, 1, file_size, file);
	if (read_size != file_size) {
		perror("Failed to read entire file");
		free(json_data);
		fclose(file);
		exit(1);
	}
	json_data[file_size] = '\0';

	fclose(file);

	cJSON *json = cJSON_Parse(json_data);
	free(json_data);
	if (!json) {
		printf("Error parsing JSON\n");
		return;
	}

	cJSON *valid_src = cJSON_GetObjectItem(json, "valid_src");

	if (cJSON_IsArray(valid_src)) {
		int size = cJSON_GetArraySize(valid_src);
		num_sessions = size;
		if (num_sessions > MAX_VALID_SESSIONS){
			printf("num_sessions > MAX_VALID_SESSIONS\n");
			exit(1);
		}
		for (int i = 0; i < num_sessions; i++) {
			cJSON *entry = cJSON_GetArrayItem(valid_src, i);
			cJSON *src_addr = cJSON_GetObjectItem(entry, "src_addr");
			cJSON *src_port = cJSON_GetObjectItem(entry, "src_port");

			if (cJSON_IsString(src_addr) && cJSON_IsNumber(src_port)) {
				// printf("  - %s:%d\n", src_addr->valuestring, src_port->valueint);
				valid_sessions[i].saddr = inet_addr(src_addr->valuestring);
				valid_sessions[i].sport = htons(src_port->valueint);
				valid_sessions[i].proto = IPPROTO_UDP;
				valid_sessions[i].daddr = addr.s_addr;
				valid_sessions[i].dport = htons(load_balancer_port);
			}
		}
	} else {
		printf("Error: valid_src is not a valid array\n");
	}

	cJSON_Delete(json); // Clean up
}

static void *configure(void)
{
	int nbackends;
	send_cmd(cfg->uds_sockfd, FLASH__GET_DST_IP_ADDR);
	recv_data(cfg->uds_sockfd, &nbackends, sizeof(int));
	if (nbackends != 1){
		printf("Firewall is linked to %d load balancers", nbackends);
		exit(1);
	}
	recv_data(cfg->uds_sockfd, load_balancer_addr, INET_ADDRSTRLEN);

	read_json_config();

	hashmap_init(&valid_sessions_map, sizeof(struct session_id), sizeof(int), MAX_VALID_SESSIONS);
	for (int session_num = 0; session_num < num_sessions; session_num++) {
		struct session_id *key = &valid_sessions[session_num];
		int val = 1;
		hashmap_insert_elem(&valid_sessions_map, (void *)key, (void *)&val);
	}
	return NULL;
}

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
			struct ethhdr *eth = pkt;
			if ((void *)(eth + 1) > pkt_end) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}

			if (eth->h_proto != htons(ETH_P_IP)) {
				send[tot_pkt_send++] = &msg.msg_iov[i];
				continue;
			}

			struct iphdr *iph = (void *)(eth + 1);
			if ((void *)(iph + 1) > pkt_end) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}

			void *next = (void *)iph + (iph->ihl << 2);

			uint16_t *sport, *dport;

			switch (iph->protocol) {
			case IPPROTO_TCP:;
				struct tcphdr *tcph = next;
				if ((void *)(tcph + 1) > pkt_end) {
					drop[tot_pkt_drop++] = &msg.msg_iov[i];
					continue;
				}

				sport = &tcph->source;
				dport = &tcph->dest;

				break;

			case IPPROTO_UDP:;
				struct udphdr *udph = next;
				if ((void *)(udph + 1) > pkt_end) {
					drop[tot_pkt_drop++] = &msg.msg_iov[i];
					continue;
				}

				sport = &udph->source;
				dport = &udph->dest;

				break;

			default:
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}

			struct session_id sid = { 0 };
			sid.saddr = iph->saddr;
			sid.daddr = iph->daddr;
			sid.proto = iph->protocol;
			sid.sport = *sport;
			sid.dport = *dport;

            // Checking only on proto, daddr, dport
			// if (sid.proto == IPPROTO_UDP && sid.daddr == inet_addr("192.168.10.1") && sid.dport == htons(80))
            // {
			// 	send[tot_pkt_send++] = &msg.msg_iov[i];
            //     continue;
	        // }
			if (hashmap_lookup_elem(&valid_sessions_map, (void*) &sid) == NULL){
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				continue;
			}
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