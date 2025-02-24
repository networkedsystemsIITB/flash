/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <stdlib.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

static bool done = false;
struct config *cfg = NULL;
struct xsk_socket_info *xsk;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
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

static void *socket_routine(void *arg)
{
	int thread_id = *(int *)arg;
	log_info("THREAD_ID: %d", thread_id);
	static __u32 nb_frags;
	int i, ret, nfds = 1, nrecv;
	int flags = FLASH__RXTX | FLASH__BACKP;
	struct pollfd fds[1] = {};
	struct xskmsghdr msg = {};

	msg.msg_iov = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));

	fds[0].fd = xsk->threads[thread_id].fd;
	fds[0].events = POLLIN;

	for (;;) {
		if (cfg->xsk->mode__poll) {
			ret = flash__poll(fds, nfds, cfg->xsk->poll_timeout);
			if (ret <= 0 || ret > 1)
				continue;
		}

		nrecv = flash__recvmsg(cfg, &xsk->threads[thread_id], &msg,
				       flags);

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &msg.msg_iov[i];
			bool eop = IS_EOP_DESC(xv->options);

			char *pkt = xv->data;

			if (!nb_frags++)
				swap_mac_addresses(pkt);

			if (eop)
				nb_frags = 0;
		}

		if (nrecv) {
			ret = flash__sendmsg(cfg, &xsk->threads[thread_id],
					     &msg, flags);
			if (ret != nrecv) {
				log_error("errno: %d/\"%s\"\n", errno,
					  strerror(errno));
				exit(EXIT_FAILURE);
			}
		}

		if (done)
			break;
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

	flash__parse_cmdline_args(argc, argv, cfg);
	flash__configure_nf(&xsk, cfg);
	flash__populate_fill_ring(xsk, cfg->umem->frame_size, cfg->n_threads,
				  cfg->offset);

	log_info("Control Plane Setup Done");

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	log_info("STARTING Data Path");

	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);
	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t),
				   &cpuset) != 0) {
		log_error("ERROR: Unable to set thread affinity: %s\n",
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	int thread_id[24];
	pthread_t *socket_thread = calloc(cfg->n_threads, sizeof(pthread_t));
	for (int i = 0; i < cfg->n_threads; i++) {
		thread_id[i] = i;
		if (pthread_create(&socket_thread[i], NULL, socket_routine,
				   thread_id + i)) {
			log_error("Error creating socket thread");
			exit(EXIT_FAILURE);
		}
		CPU_ZERO(&cpuset);
		CPU_SET(i + cfg->offset + 1, &cpuset);
		if (pthread_setaffinity_np(socket_thread[i], sizeof(cpu_set_t),
					   &cpuset) != 0) {
			log_error("ERROR: Unable to set thread affinity: %s\n",
				  strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	for (int i = 0; i < cfg->n_threads; i++) {
		log_info("+++++++++++");
		if (pthread_join(socket_thread[i], NULL)) {
			log_error("Error joining socket thread");
			exit(EXIT_FAILURE);
		}
	}

	log_info("REACHING HERE!!!!");

	flash__xsk_close(cfg, xsk);

	return EXIT_SUCCESS;
}
