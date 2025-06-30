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

// clang-format off
static const char *firewall_options[] = {
	"-c <num>\tStart CPU (default: 0)",
	"-e <num>\tEnd CPU (default: 0)",
	"-s <num>\tStats CPU (default: 1)",
	NULL
};
// clang-format on

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

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
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
	uint32_t i, nrecv, wsend, nsend, wdrop, ndrop;
	struct sock_args *a = (struct sock_args *)arg;

	int socket_id = a->socket_id;

	log_info("SOCKET_ID: %d", socket_id);	

	xsk = nf->thread[socket_id]->socket;

	xskvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!xskvecs) {
		log_error("ERROR: Memory allocation failed for xskvecs");
		return NULL;
	}

	sendvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!sendvecs) {
		log_error("ERROR: Memory allocation failed for sendvecs");
		free(xskvecs);
		return NULL;
	}

	dropvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!dropvecs) {
		log_error("ERROR: Memory allocation failed for dropvecs");
		free(xskvecs);
		free(sendvecs);
		return NULL;
	}

	fds[0].fd = nf->thread[socket_id]->socket->fd;
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

			void *pkt = xv->data;
			void *pkt_end = pkt + xv->len;

			struct ethhdr *eth = pkt;
			if ((void *)(eth + 1) > pkt_end) {
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			if (eth->h_proto != htons(ETH_P_IP)) {
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			struct iphdr *iph = (void *)(eth + 1);
			if ((void *)(iph + 1) > pkt_end) {
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			void *next = (void *)iph + (iph->ihl << 2);

			uint16_t *sport, *dport;

			switch (iph->protocol) {
			case IPPROTO_TCP:;
				struct tcphdr *tcph = next;
				if ((void *)(tcph + 1) > pkt_end) {
					dropvecs[wdrop++] = xskvecs[i];
					continue;
				}

				sport = &tcph->source;
				dport = &tcph->dest;

				break;

			case IPPROTO_UDP:;
				struct udphdr *udph = next;
				if ((void *)(udph + 1) > pkt_end) {
					dropvecs[wdrop++] = xskvecs[i];
					continue;
				}

				sport = &udph->source;
				dport = &udph->dest;

				break;

			default:
				dropvecs[wdrop++] = xskvecs[i];
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
					dropvecs[wdrop++] = xskvecs[i];
					invalid = true;
					break;
				}
			}
			if (!invalid)
				sendvecs[wsend++] = xskvecs[i];
		}

		if (nrecv) {
			nsend = flash__sendmsg(cfg, xsk, sendvecs, wsend);
			ndrop = flash__dropmsg(cfg, xsk, dropvecs, wdrop);
			if (nsend != wsend || ndrop != wdrop) {
				log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
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
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	cfg->app_name = "Firewall Application";
	cfg->app_options = firewall_options;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;
	
	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;
	
	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	log_info("Control Plane Setup Done");

	configure();
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