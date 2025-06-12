/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * l2fwd: A simple NF that forwards packets between two interfaces
 * after swapping or modifying MAC addresses.
 */
#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

bool done = false;
struct config *cfg = NULL;
struct nf *nf = NULL;

static void int_exit(int sig)
{
	log_debug("Received Signal: %d", sig);
	done = true;
}

struct appconf {
	int cpu_start;
	int cpu_end;
	int stats_cpu;
	bool sriov;
	uint8_t *dest_ether_addr_octet;
} app_conf;

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

// clang-format off
static const char *l2fwd_options[] = {
    "-c <num>\tStart CPU (default: 0)",
    "-e <num>\tEnd CPU (default: 0)",
    "-s <num>\tStats CPU (default: 1)",
    "-S <mac>\tEnable SR-IOV mode and set dest MAC address",
    NULL
};
// clang-format on

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

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

struct sock_args {
	int socket_id;
	// int *next;
	// int next_size;
};

static void *socket_routine(void *arg)
{
	int ret;
	nfds_t nfds = 1;
	struct socket *xsk;
	struct xskvec *xskvecs;
	struct pollfd fds[1] = {};
	uint32_t i, nrecv, nsend, nb_frags = 0;
	struct sock_args *a = (struct sock_args *)arg;

	log_debug("Socket ID: %d", a->socket_id);
	xsk = nf->thread[a->socket_id]->socket;

	xskvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!xskvecs) {
		log_error("Failed to allocate xskvecs array");
		return NULL;
	}

	fds[0].fd = xsk->fd;
	fds[0].events = POLLIN;

	for (;;) {
		ret = flash__poll(cfg, xsk, fds, nfds);
		if (!(ret == 1 || ret == -2))
			continue;

		nrecv = flash__recvmsg(cfg, xsk, xskvecs, cfg->xsk->batch_size);
		for (i = 0; i < nrecv; i++) {
			char *pkt = xskvecs[i].data;

			if (!nb_frags++)
				app_conf.sriov ? update_dest_mac(pkt) : swap_mac_addresses(pkt);

			if (IS_EOP_DESC(xskvecs[i].options))
				nb_frags = 0;
		}

		if (nrecv) {
			nsend = flash__sendmsg(cfg, xsk, xskvecs, nrecv);
			if (nsend != nrecv) {
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

	cfg = calloc(1, sizeof(struct config));
	if (!cfg) {
		log_error("ERROR: Memory allocation failed");
		exit(EXIT_FAILURE);
	}

	cfg->app_name = "L2 Forwarding Application";
	cfg->app_options = l2fwd_options;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;

	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;

	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	log_info("Control Plane setup done...");

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	log_info("Starting Data Path...");

	args = calloc(cfg->total_sockets, sizeof(struct sock_args));
	if (!args) {
		log_error("ERROR: Memory allocation failed for sock_args");
		goto out_cfg;
	}

	for (int i = 0; i < cfg->total_sockets; i++) {
		args[i].socket_id = i;
		// args[i].next = nf->next;
		// args[i].next_size = nf->next_size;

		// log_debug("Next Size ::: %d", args[i].next_size);

		// for (int i = 0; i < args[i].next_size; i++)
		// 	log_debug("Next Item [%d] ::: %d", i, nf->next[i]);

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
	free(args);
out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
