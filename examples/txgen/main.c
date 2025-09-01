/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * txgen: A packet generator that transmits Ethernet+IPv4+UDP frames with
 * configurable addresses and ports.
 */
#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <stdlib.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

volatile bool done = false;
struct config *cfg = NULL;
struct nf *nf = NULL;
uint8_t *packet_template = NULL;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
}

struct appconf {
	int cpu_start;
	int cpu_end;
	int stats_cpu;
	bool custom_src_ether_addr;
	bool custom_dest_ether_addr;
	uint8_t src_ether_addr_octet[6];
	uint8_t dest_ether_addr_octet[6];
	uint32_t src_ip;
	uint32_t dest_ip;
	uint16_t src_port;
	uint16_t dest_port;
	uint16_t payload_len;
} app_conf;

// clang-format off
static const char *txgen_options[] = {
    "-c <num>\tStart CPU (default: 0)",
    "-e <num>\tEnd CPU (default: 0)",
    "-s <num>\tStats CPU (default: 1)",
	"-S <mac>\tSrc MAC address to use (default: NIC MAC address)",
	"-D <mac>\tDest MAC address to use (default: NIC MAC address)",
	"-A <ipv4>\tSrc IPv4 address to use (default: 192.168.1.1)",
	"-B <ipv4>\tDest IPv4 address to use (default: 192.168.2.1)",
	"-P <port>\tSrc port to use (default: 1234)",
	"-Q <port>\tDest port to use (default: 5678)",
	"-L <len>\tPayload length (default: 5 bytes)",
    NULL
};
// clang-format on

static int parse_mac(const char *str, uint8_t *mac)
{
	int vals[6];
	if (sscanf(str, "%x:%x:%x:%x:%x:%x", &vals[0], &vals[1], &vals[2], &vals[3], &vals[4], &vals[5]) != 6) {
		log_error("Invalid MAC address: %s", str);
		return -1;
	}
	for (int i = 0; i < 6; i++)
		mac[i] = (uint8_t)vals[i];

	return 0;
}

static int parse_ip(const char *str, uint32_t *ip)
{
	struct in_addr addr;
	if (inet_aton(str, &addr) == 0) {
		log_error("Invalid IPv4 address: %s", str);
		return -1;
	}
	*ip = addr.s_addr;

	return 0;
}

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->src_ip = htonl(0xC0A80101);
	app_conf->dest_ip = htonl(0xC0A80201);
	app_conf->src_port = htons(1234);
	app_conf->dest_port = htons(5678);
	app_conf->payload_len = 5;
	app_conf->custom_src_ether_addr = false;
	app_conf->custom_dest_ether_addr = false;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "hc:e:s:S:D:A:B:P:Q:L:")) != -1)
		switch (c) {
		case 'h':
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		case 'c':
			app_conf->cpu_start = atoi(optarg);
			break;
		case 'e':
			app_conf->cpu_end = atoi(optarg);
			break;
		case 's':
			app_conf->stats_cpu = atoi(optarg);
			break;
		case 'S':
			if (parse_mac(optarg, app_conf->src_ether_addr_octet) < 0)
				return -1;
			app_conf->custom_src_ether_addr = true;
			break;
		case 'D':
			if (parse_mac(optarg, app_conf->dest_ether_addr_octet) < 0)
				return -1;
			app_conf->custom_dest_ether_addr = true;
			break;
		case 'A':
			if (parse_ip(optarg, &app_conf->src_ip) < 0)
				return -1;
			break;
		case 'B':
			if (parse_ip(optarg, &app_conf->dest_ip) < 0)
				return -1;
			break;
		case 'P':
			app_conf->src_port = htons(atoi(optarg));
			break;
		case 'Q':
			app_conf->dest_port = htons(atoi(optarg));
			break;
		case 'L':
			app_conf->payload_len = atoi(optarg);
			if (app_conf->payload_len > 1500) {
				log_error("Invalid payload length: %d.", app_conf->payload_len);
				return -1;
			}
			break;
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}

	return 0;
}

static uint16_t csum16(const void *data, size_t len)
{
	const uint16_t *buf = (const uint16_t *)data;
	uint32_t sum = 0;

	while (len > 1) {
		sum += *buf++;
		len -= 2;
	}

	if (len)
		sum += *(const uint8_t *)buf;

	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return (uint16_t)(~sum);
}

static int setup_packet(void *data)
{
	struct ether_header *eth = (struct ether_header *)data;
	struct iphdr *ip = (struct iphdr *)(eth + 1);
	struct udphdr *udp = (struct udphdr *)(ip + 1);
	struct ether_addr tmp_addr;

	if (app_conf.custom_src_ether_addr)
		memcpy(eth->ether_shost, app_conf.src_ether_addr_octet, ETH_ALEN);
	else {
		if (flash__get_macaddr(cfg, &tmp_addr) < 0) {
			log_error("Failed to get source MAC address");
			return -1;
		}
		memcpy(eth->ether_shost, tmp_addr.ether_addr_octet, ETH_ALEN);
	}

	if (app_conf.custom_dest_ether_addr)
		memcpy(eth->ether_dhost, app_conf.dest_ether_addr_octet, ETH_ALEN);
	else {
		if (flash__get_macaddr(cfg, &tmp_addr) < 0) {
			log_error("Failed to get destination MAC address");
			return -1;
		}
		memcpy(eth->ether_dhost, tmp_addr.ether_addr_octet, ETH_ALEN);
	}
	eth->ether_type = htons(ETH_P_IP);

	ip->ihl = 5;
	ip->version = 4;
	ip->tos = 0;
	ip->tot_len = htons(sizeof(struct iphdr) + sizeof(struct udphdr) + app_conf.payload_len);
	ip->id = htons(0x1234);
	ip->frag_off = 0;
	ip->ttl = 64;
	ip->protocol = IPPROTO_UDP;
	ip->check = 0;
	ip->saddr = app_conf.src_ip;
	ip->daddr = app_conf.dest_ip;
	ip->check = csum16(ip, sizeof(struct iphdr));

	udp->source = app_conf.src_port;
	udp->dest = app_conf.dest_port;
	udp->len = htons(sizeof(struct udphdr) + app_conf.payload_len);
	udp->check = 0;

	char *payload = (char *)(udp + 1);
	memset(payload, 'A', app_conf.payload_len);

	return 0;
}

struct sock_args {
	int socket_id;
};

static void *socket_routine(void *arg)
{
	struct socket *xsk;
	struct xskvec *xskvecs;
	uint32_t i, nalloc, nsend;
	struct sock_args *a = (struct sock_args *)arg;

	log_debug("Socket ID: %d", a->socket_id);
	xsk = nf->thread[a->socket_id]->socket;

	xskvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!xskvecs) {
		log_error("Failed to allocate xskvecs array");
		return NULL;
	}

	size_t packet_size = sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct udphdr) + app_conf.payload_len;

	for (;;) {
		nalloc = flash__allocmsg(cfg, xsk, xskvecs, cfg->xsk->batch_size);
		for (i = 0; i < nalloc; i++) {
			memcpy(xskvecs[i].data, packet_template, packet_size);
			xskvecs[i].len = packet_size;
			xskvecs[i].options = 0;
		}

		if (nalloc) {
			nsend = flash__sendmsg(cfg, xsk, xskvecs, nalloc);
			if (nsend != nalloc) {
				log_error("errno: %d/\"%s\"", errno, strerror(errno));
				break;
			}
		}

		if (done)
			break;
	}

	free(xskvecs);
	return NULL;
}

int main(int argc, char **argv)
{
	int shift;
	struct sock_args *args;
	struct stats_conf stats_cfg = { NULL };
	cpu_set_t cpuset;
	pthread_t socket_thread, stats_thread;
	size_t packet_size;

	cfg = calloc(1, sizeof(struct config));
	if (!cfg) {
		log_error("ERROR: Memory allocation failed");
		exit(EXIT_FAILURE);
	}

	cfg->app_name = "Traffic Generation Application";
	cfg->app_options = txgen_options;
	cfg->done = &done;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;

	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;

	if (cfg->rx_first) {
		log_error("ERROR: tx_first should be enabled in txgen");
		goto out_cfg;
	}

	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	log_info("Control Plane setup done...");

	packet_size = sizeof(struct ether_header) + sizeof(struct iphdr) + sizeof(struct udphdr) + app_conf.payload_len;
	log_debug("Packet size: %zu bytes", packet_size);

	packet_template = (uint8_t *)calloc(1, packet_size);
	if (!packet_template) {
		log_error("ERROR: Memory allocation failed for packet template");
		goto out_cfg_close;
	}

	if (setup_packet(packet_template) < 0) {
		log_error("ERROR: Failed to setup packet template");
		goto out_pkt;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	log_info("Starting Data Path...");

	args = calloc(cfg->total_sockets, sizeof(struct sock_args));
	if (!args) {
		log_error("ERROR: Memory allocation failed for sock_args");
		goto out_pkt;
	}

	for (int i = 0; i < cfg->total_sockets; i++) {
		args[i].socket_id = i;

		if (pthread_create(&socket_thread, NULL, socket_routine, &args[i])) {
			log_error("Error creating socket thread");
			goto out_args;
		}

		CPU_ZERO(&cpuset);
		CPU_SET((i % (app_conf.cpu_end - app_conf.cpu_start + 1)) + app_conf.cpu_start, &cpuset);
		if (pthread_setaffinity_np(socket_thread, sizeof(cpu_set_t), &cpuset) != 0) {
			log_error("ERROR: Unable to set thread affinity: %s", strerror(errno));
			goto out_args;
		}

		if (pthread_detach(socket_thread) != 0) {
			log_error("ERROR: Unable to detach thread: %s", strerror(errno));
			goto out_args;
		}
	}

	stats_cfg.nf = nf;
	stats_cfg.cfg = cfg;

	if (pthread_create(&stats_thread, NULL, flash__stats_thread, &stats_cfg)) {
		log_error("Error creating statistics thread");
		goto out_args;
	}
	CPU_ZERO(&cpuset);
	CPU_SET(app_conf.stats_cpu, &cpuset);
	if (pthread_setaffinity_np(stats_thread, sizeof(cpu_set_t), &cpuset) != 0) {
		log_error("ERROR: Unable to set thread affinity: %s", strerror(errno));
		goto out_args;
	}

	if (pthread_detach(stats_thread) != 0) {
		log_error("ERROR: Unable to detach thread: %s", strerror(errno));
		goto out_args;
	}

	flash__wait(cfg);
	flash__xsk_close(cfg, nf);

	exit(EXIT_SUCCESS);

out_args:
	done = true;
	free(args);
out_pkt:
	free(packet_template);
out_cfg_close:
	sleep(1);
	flash__xsk_close(cfg, nf);
out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
