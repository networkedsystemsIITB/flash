/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * l2fwd: A simple NF that forwards packets between two interfaces
 */

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>

#include <flash_nf.h>
#include <flash_params.h>
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

static void parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
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

	while ((c = getopt(argc, argv, "c:e:s:S:")) != -1)
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
		case 'S':
			app_conf->dest_ether_addr_octet = get_mac_addr(optarg);
			app_conf->sriov = true;
			break;
		default:
			abort();
		}
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

struct Args {
	int socket_id;
	int *next;
	int next_size;
};

static void *socket_routine(void *arg)
{
	struct Args *a = (struct Args *)arg;
	int socket_id = a->socket_id;
	log_info("SOCKET_ID: %d", socket_id);
	static __u32 nb_frags;
	int i, ret, nfds = 1, nrecv;
	struct pollfd fds[1] = {};
	struct xskmsghdr msg = {};

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
		struct xskvec *send[nrecv];
		unsigned int tot_pkt_send = 0;
		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &msg.msg_iov[i];
			bool eop = IS_EOP_DESC(xv->options);

			char *pkt = xv->data;

			if (!nb_frags++)
				app_conf.sriov ? update_dest_mac(pkt) : swap_mac_addresses(pkt);

			send[tot_pkt_send++] = &msg.msg_iov[i];
			if (eop)
				nb_frags = 0;
		}

		if (nrecv) {
			ret = flash__sendmsg(cfg, nf->thread[socket_id]->socket, send, tot_pkt_send);
			if (ret != nrecv) {
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
	set_nonblocking(cfg->uds_sockfd);
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
