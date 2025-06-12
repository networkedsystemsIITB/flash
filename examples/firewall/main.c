#include <flash_nf.h>
#include <flash_params.h>
#include <flash_uds.h>

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>
#include <log.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include "murmurhash.h"

#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define IP_STRLEN 16
#define PROTO_STRLEN 4
#define IFNAME_STRLEN 256
#define NUM_INVALID_SESSIONS 1000

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

uint32_t invalid_sessions[NUM_INVALID_SESSIONS];

static void *configure(void)
{
	// Initialise invalid_sessions with random numbers
	srand(time(NULL)); // Seed only once before generating any random numbers

	for (int i = 0; i < NUM_INVALID_SESSIONS; i++) {
		int r = rand(); // Different value each iteration
		invalid_sessions[i] = r;
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
			ret = flash__oldpoll(nf->thread[socket_id]->socket, fds, nfds, cfg->xsk->poll_timeout);
			if (ret <= 0 || ret > 1)
				continue;
		}
		nrecv = flash__oldrecvmsg(cfg, nf->thread[socket_id]->socket, &msg);

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
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
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

			// Find murmurhash of sid
			uint32_t sid_hash = murmurhash((void *)&sid, sizeof(struct session_id), 0);
			bool invalid = false;
			for (int i = 0; i < NUM_INVALID_SESSIONS; i++) {
				if (invalid_sessions[i] == sid_hash) {
					drop[tot_pkt_drop++] = &msg.msg_iov[i];
					invalid = true;
					break;
				}
			}
			if (!invalid)
				send[tot_pkt_send++] = &msg.msg_iov[i];
		}

		if (nrecv) {
			size_t ret_send = flash__oldsendmsg(cfg, nf->thread[socket_id]->socket, send, tot_pkt_send);
			size_t ret_drop = flash__olddropmsg(cfg, nf->thread[socket_id]->socket, drop, tot_pkt_drop);
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

	flash__wait(cfg);

	flash__xsk_close(cfg, nf);

	return EXIT_SUCCESS;
}