/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * simplefwd: A simple NF that forwards packets without modification
 */
#include <signal.h>
#include <pthread.h>

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
} app_conf;

// clang-format off
static const char *l2fwd_options[] = {
    "-c <num>\tStart CPU (default: 0)",
    "-e <num>\tEnd CPU (default: 0)",
    "-s <num>\tStats CPU (default: 1)",
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

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "hc:e:s:")) != -1)
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
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}

	return 0;
}

static void do_nothing(void *data)
{
	/* This is stupid but it makes sure that compiler doesn't through any errors */
	(void)data;
}

struct sock_args {
	int socket_id;
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
		log_error("Failed to allocate send array");
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
				do_nothing(pkt);

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
