/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * fwddrop: unit-test to check forward and drop capabilities of Flash library
 */
#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <stdlib.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

bool done = false;
struct config *cfg = NULL;
struct nf *nf = NULL;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
}

struct appconf {
	int cpu_start;
	int cpu_end;
	int stats_cpu;
	int fwd_ratio;
	bool sriov;
	uint8_t dest_ether_addr_octet[6];
} app_conf;

// clang-format off
static const char *fwddrop_options[] = {
    "-c <num>\tStart CPU (default: 0)",
    "-e <num>\tEnd CPU (default: 0)",
    "-s <num>\tStats CPU (default: 1)",
	"-r <num>\tForward ratio percentage (default: 50)",
    "-S <mac>\tEnable SR-IOV mode and set dest MAC address",
    NULL
};
// clang-format on

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	int ethaddr[6];
	opterr = 0;

	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->sriov = false;
	app_conf->fwd_ratio = 50;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "hc:e:s:r:S:")) != -1)
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
		case 'r':
			app_conf->fwd_ratio = atoi(optarg);
			if (app_conf->fwd_ratio < 0 || app_conf->fwd_ratio > 100) {
				log_error("Invalid forward ratio: %d. Must be between 0 and 100.", app_conf->fwd_ratio);
				return -1;
			}
			break;
		case 'S':
			if (sscanf(optarg, "%x:%x:%x:%x:%x:%x", &ethaddr[0], &ethaddr[1], &ethaddr[2], &ethaddr[3], &ethaddr[4],
				   &ethaddr[5]) != 6) {
				log_error("Invalid MAC address format: %s", optarg);
				return -1;
			}
			for (int i = 0; i < 6; i++)
				app_conf->dest_ether_addr_octet[i] = (uint8_t)ethaddr[i];
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
};

static void *socket_routine(void *arg)
{
	int ret;
	nfds_t nfds = 1;
	struct socket *xsk;
	struct pollfd fds[1] = {};
	struct xskvec *xskvecs, *sendvecs, *dropvecs;
	uint32_t i, nrecv, wsend, nsend, wdrop, ndrop, pcount, nb_frags = 0;
	struct sock_args *a = (struct sock_args *)arg;

	log_debug("Socket ID: %d", a->socket_id);
	xsk = nf->thread[a->socket_id]->socket;

	xskvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!xskvecs) {
		log_error("Failed to allocate xskvecs array");
		return NULL;
	}

	sendvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!sendvecs) {
		log_error("Failed to allocate sendvecs array");
		return NULL;
	}

	dropvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!dropvecs) {
		log_error("Failed to allocate dropvecs array");
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
		pcount = 0;
		for (i = 0; i < nrecv; i++) {
			char *pkt = xskvecs[i].data;

			if (!nb_frags++)
				app_conf.sriov ? update_dest_mac(pkt) : swap_mac_addresses(pkt);

			if (IS_EOP_DESC(xskvecs[i].options))
				nb_frags = 0;

			if ((int)(pcount * 100 / nrecv) < app_conf.fwd_ratio) {
				sendvecs[wsend++] = xskvecs[i];
			} else {
				dropvecs[wdrop++] = xskvecs[i];
			}
			pcount++;
		}

		if (nrecv) {
			nsend = flash__sendmsg(cfg, xsk, sendvecs, wsend);
			ndrop = flash__dropmsg(cfg, xsk, dropvecs, wdrop);
			if (nsend != wsend || ndrop != wdrop) {
				log_error("errno: %d/\"%s\"", errno, strerror(errno));
				break;
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
		log_error("ERROR: Memory allocation failed");
		exit(EXIT_FAILURE);
	}

	cfg->app_name = "Unit Test: Forward and Drop Application";
	cfg->app_options = fwddrop_options;

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
out_cfg_close:
	sleep(1);
	flash__xsk_close(cfg, nf);
out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
