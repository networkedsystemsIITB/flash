/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * ip4ping: A simple NF that responds to ICMP echo requests
 */

#include <flash_nf.h>
#include <flash_params.h>

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <netinet/in.h>
#include <log.h>

volatile bool done = false;
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
	bool sriov;
	uint8_t *dest_ether_addr_octet;
} app_conf;

// clang-format off
static const char *ip4ping_options[] = {
	"-c <num>\tStart CPU (default: 0)",
	"-e <num>\tEnd CPU (default: 0)",
	"-s <num>\tStats CPU (default: 1)",
	"-S <mac>\tEnable SR-IOV mode and set dest MAC address",
	NULL
};
// clang-format on

static int hex2int(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	return -1;
}

static uint8_t *get_mac_addr(char *mac_addr)
{
	uint8_t *dest_ether_addr_octet = (uint8_t *)malloc(6 * sizeof(uint8_t));
	for (int i = 0; i < 6; i++) {
		dest_ether_addr_octet[i] = hex2int(mac_addr[0]) * 16;
		mac_addr++;
		dest_ether_addr_octet[i] += hex2int(mac_addr[0]);
		mac_addr += 2;
	}
	return dest_ether_addr_octet;
}

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->sriov = false;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "hc:e:s:S:")) != -1)
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
			app_conf->dest_ether_addr_octet = get_mac_addr(optarg);
			app_conf->sriov = true;
			break;
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}

	return 0;
}

static void update_dest_mac(void *data)
{
	struct ether_header *eth = (struct ether_header *)data;
	struct ether_addr *dst_addr = (struct ether_addr *)&eth->ether_dhost;
	struct ether_addr tmp = {
		.ether_addr_octet = {
			app_conf.dest_ether_addr_octet[0],
			app_conf.dest_ether_addr_octet[1],
			app_conf.dest_ether_addr_octet[2],
			app_conf.dest_ether_addr_octet[3],
			app_conf.dest_ether_addr_octet[4],
			app_conf.dest_ether_addr_octet[5],
		},
	};
	*dst_addr = tmp;
}

static void swap_mac_addresses(void *data)
{
	struct ether_header *eth = (struct ether_header *)data;
	struct ether_addr *src_addr = (struct ether_addr *)&eth->ether_shost;
	struct ether_addr *dst_addr = (struct ether_addr *)&eth->ether_dhost;
	struct ether_addr tmp;

	tmp = *src_addr;
	*src_addr = *dst_addr;
	*dst_addr = tmp;
}

static inline __sum16 csum16_add(__sum16 csum, __be16 addend)
{
	uint16_t res = (uint16_t)csum;

	res += (__u16)addend;
	return (__sum16)(res + (res < (__u16)addend));
}

static inline __sum16 csum16_sub(__sum16 csum, __be16 addend)
{
	return csum16_add(csum, ~addend);
}

static inline void csum_replace2(__sum16 *sum, __be16 old, __be16 new)
{
	*sum = ~csum16_add(csum16_sub(~(*sum), old), new);
}

struct sock_args {
	int socket_id;
};

unsigned int count = 1;

static void *socket_routine(void *arg)
{
	int ret;
	nfds_t nfds = 1;
	struct socket *xsk;
	struct xskvec *xskvecs, *sendvecs, *dropvecs;
	struct pollfd fds[1] = {};
	uint32_t i, nrecv, nsend, wsend, wdrop, ndrop;
	struct sock_args *a = (struct sock_args *)arg;

	int socket_id = a->socket_id;
	log_info("SOCKET_ID: %d", socket_id);
	xsk = nf->thread[socket_id]->socket;

	xskvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!xskvecs) {
		log_error("Failed to allocate xskvecs array");
		return NULL;
	}

	sendvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!sendvecs) {
		log_error("Failed to allocate sendvecs array");
		free(xskvecs);
		return NULL;
	}

	dropvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!dropvecs) {
		log_error("Failed to allocate dropvecs array");
		free(xskvecs);
		free(sendvecs);
		return NULL;
	}

	fds[0].fd = xsk->fd;
	fds[0].events = POLLIN;

	for (;;) {
		ret = flash__poll(cfg, xsk, fds, nfds);
		if (!(ret == 1 || ret == -2))
			continue;

		nrecv = flash__recvmsg(cfg, xsk, xskvecs, cfg->xsk->batch_size);
		wsend = 0;
		wdrop = 0;

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &xskvecs[i];
			void *data = xv->data;
			uint32_t len = xv->len;

			void *data_end = data + len;
			uint8_t tmp_mac[ETH_ALEN];
			struct in_addr tmp_ip;
			struct ethhdr *eth = (struct ethhdr *)data;
			if ((void *)(eth + 1) > data_end)
				dropvecs[wdrop++] = xskvecs[i];

			if (ntohs(eth->h_proto) != ETH_P_IP)
				dropvecs[wdrop++] = xskvecs[i];

			struct iphdr *ip = (struct iphdr *)(eth + 1);
			if ((void *)(ip + 1) > data_end)
				dropvecs[wdrop++] = xskvecs[i];

			struct icmphdr *icmp = (struct icmphdr *)(ip + 1);
			if ((void *)(icmp + 1) > data_end)
				dropvecs[wdrop++] = xskvecs[i];

			if (ntohs(eth->h_proto) != ETH_P_IP || len < (sizeof(*eth) + sizeof(*ip) + sizeof(*icmp)) ||
			    ip->protocol != IPPROTO_ICMP || icmp->type != ICMP_ECHO) {
				sendvecs[wsend++] = xskvecs[i];

				if (app_conf.sriov) {
					swap_mac_addresses(data);
					update_dest_mac(data);
				}

				continue;
			}

			memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
			memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
			memcpy(eth->h_source, tmp_mac, ETH_ALEN);

			memcpy(&tmp_ip, &ip->saddr, sizeof(tmp_ip));
			memcpy(&ip->saddr, &ip->daddr, sizeof(tmp_ip));
			memcpy(&ip->daddr, &tmp_ip, sizeof(tmp_ip));

			icmp->type = ICMP_ECHOREPLY;

			csum_replace2(&icmp->checksum, htons(ICMP_ECHO << 8), htons(ICMP_ECHOREPLY << 8));
		}

		if (nrecv) {
			nsend = flash__sendmsg(cfg, xsk, sendvecs, wsend);
			ndrop = flash__dropmsg(cfg, xsk, dropvecs, wdrop);

			if (nsend != wsend || ndrop != wdrop) {
				log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

		if (done)
			break;
	}
	free(xskvecs);
	free(sendvecs);
	free(dropvecs);
	return NULL;
}

int main(int argc, char **argv)
{
	int shift;
	struct sock_args *args;
	struct stats_conf stats_cfg = { NULL };
	cpu_set_t cpuset;
	pthread_t socket_thread, stats_thread;

	cfg = calloc(1, sizeof(struct config));
	if (!cfg) {
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	cfg->app_name = "IP4 Ping Application";
	cfg->app_options = ip4ping_options;
	cfg->done = &done;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;

	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;

	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	log_info("Control Plane Setup Done");

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	log_info("STARTING Data Path");

	args = calloc(cfg->total_sockets, sizeof(struct sock_args));
	if (!args) {
		log_error("ERROR: Memory allocation failed for sock_args");
		goto out_cfg_close;
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
			log_error("ERROR: Unable to set thread affinity: %s\n", strerror(errno));
			goto out_args;
		}

		if (pthread_detach(socket_thread) != 0) {
			log_error("ERROR: Unable to detach thread: %s\n", strerror(errno));
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
		log_error("ERROR: Unable to set thread affinity: %s\n", strerror(errno));
		goto out_args;
	}

	if (pthread_detach(stats_thread) != 0) {
		log_error("ERROR: Unable to detach thread: %s\n", strerror(errno));
		goto out_args;
	}
	flash__wait(cfg);
	flash__xsk_close(cfg, nf);

	exit(EXIT_SUCCESS);

out_args:
	done = true;
	free(args);
out_cfg_close:
	sleep(1);
	flash__xsk_close(cfg, nf);
out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
