/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * fwddrop: unit-test to check forward and drop capabilities of Flash framework
 * We store pointers to msg.iov we want to drop in one array, and those we wish to send in another array
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

struct Args {
	int socket_id;
	int *next;
	int next_size;
};

unsigned int count = 1;

static void *socket_routine(void *arg)
{
	struct Args *a = (struct Args *)arg;
	int socket_id = a->socket_id;
	int *next = a->next;
	int next_size = a->next_size;
	log_info("SOCKET_ID: %d", socket_id);
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
		struct xskvec *send[nrecv];
		unsigned int tot_pkt_drop = 0;
		unsigned int tot_pkt_send = 0;

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &msg.msg_iov[i];
			void *data = xv->data;

			uint8_t tmp_mac[ETH_ALEN];
			struct ethhdr *eth = (struct ethhdr *)data;

			memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
			memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
			memcpy(eth->h_source, tmp_mac, ETH_ALEN);

			/* fwd 50% packets and drop 50% packets */
			if (count == 1) {
				send[tot_pkt_send++] = &msg.msg_iov[i];
				count = 0;
			} else {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				count = 1;
			}
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
	flash__populate_fill_ring(nf->thread, cfg->umem->frame_size, cfg->total_sockets, cfg->umem_offset);

	log_info("Control Plane Setup Done");

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
