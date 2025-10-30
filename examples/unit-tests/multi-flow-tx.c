/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <signal.h>
#include <pthread.h>
#include <locale.h>
#include <stdlib.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

#include <netinet/in.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#define TEST_PORT 8080

volatile bool done = false;
struct config *cfg = NULL;
struct nf *nf;
struct test_stats *stats_arr;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
}

struct testHeader {
	uint8_t lastHop;
	uint8_t hopCount;
	uint64_t pktId;
	uint16_t old_dst;
};

struct test_stats {
	uint64_t pkt_count;
};

struct appconf {
	int cpu_start;
	int cpu_end;
	int stats_cpu;
	int hops;
	__u64 burn_cycles;
	bool variable;
	__u64 variable_start;
	__u64 variable_end;
} app_conf;

#if defined(__ARM_ARCH_ISA_A64)
// ARM64 based implementation
static inline __u64 rdtsc(void)
{
	__u64 cntvct;
	asm volatile("mrs %0, cntvct_el0; " : "=r"(cntvct)::"memory");
	return cntvct;
}

static inline __u64 rdtsc_precise(void)
{
	__u64 cntvct;
	asm volatile("isb; mrs %0, cntvct_el0; isb; " : "=r"(cntvct)::"memory");
	return cntvct;
}
#elif defined(__x86_64__)
// AMD64 based implementation
static inline __u64 rdtsc(void)
{
	union {
		__u64 tsc_64;
		struct {
			__u32 lo_32;
			__u32 hi_32;
		};
	} tsc;

	asm volatile("rdtsc" : "=a"(tsc.lo_32), "=d"(tsc.hi_32));

	return tsc.tsc_64;
}

static inline __u64 rdtsc_precise(void)
{
	asm volatile("mfence");
	return rdtsc();
}
#endif

static void burn_cycles(__u64 cycles_to_burn)
{
	__u64 start = rdtsc();
	while ((rdtsc() - start) < cycles_to_burn) {
		// Burn cycles
	}
}

static const char *flow_bp_options[] = { "-c <num>\tStart CPU (default: 0)",
					 "-e <num>\tEnd CPU (default: 0)",
					 "-s <num>\tStats CPU (default: 1)",
					 "-h <num>\tNumber of hops (default: 1)",
					 "-B <num>\tBurn cycles (default: 0)",
					 "-v\t\tEnable variable-length packets (default: disabled)",
					 "-a <num>\tVariable start value (default: 0)",
					 "-z <num>\tVariable end value (default: 0)",
					 NULL };

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->hops = 1;
	app_conf->burn_cycles = 0;
	app_conf->variable = false;
	app_conf->variable_start = 0;
	app_conf->variable_end = 0;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "c:e:s:h:B:va:z:")) != -1)
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
		case 'h':
			app_conf->hops = atoi(optarg);
			break;
		case 'B':
			app_conf->burn_cycles = atoi(optarg);
			break;
		case 'v':
			app_conf->variable = true;
			break;
		case 'a':
			app_conf->variable_start = atoi(optarg);
			break;
		case 'z':
			app_conf->variable_end = atoi(optarg);
			break;
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}
	return 0;
}

static void process_packets(void *data, __u32 *len)
{
	void *pos = data;
	void *data_end = data + *len;

	struct ethhdr *eth = (struct ethhdr *)pos;
	if ((void *)(eth + 1) > data_end) {
		log_error("Ethernet header is not valid");
		return;
	}

	if (eth->h_proto != htons(ETH_P_IP)) {
		log_error("Ethernet protocol is not IP");
		return;
	}

	pos = eth + 1;

	struct iphdr *iph = pos;
	size_t hdrsize;

	if ((void *)iph + 1 > data_end) {
		log_error("IP header is not valid");
		return;
	}

	hdrsize = iph->ihl * 4;
	/* Sanity check packet field is valid */
	if (hdrsize < sizeof(*iph)) {
		log_error("IP header size is invalid");
		return;
	}

	if (iph->protocol != IPPROTO_UDP) {
		log_error("IP protocol is not UDP");
		return;
	}

	/* Variable-length IPv4 header, need to use byte-based arithmetic */
	if (pos + hdrsize > data_end) {
		log_error("IP header is not valid");
		return;
	}

	pos += hdrsize;

	size_t payload_len;
	struct udphdr *udphdr = pos;

	if ((void *)udphdr + 1 > data_end) {
		log_error("UDP header is not valid");
		return;
	}

	pos = udphdr + 1;
	payload_len = ntohs(udphdr->len) - sizeof(struct udphdr);

	size_t testHeaderLen = sizeof(struct testHeader);
	void *payload_end = pos + payload_len;

	struct testHeader *testHeader = NULL;

	/* First NF */
	if (ntohs(udphdr->dest) != TEST_PORT) {
		// Append test header at the end of the UDP payload
		testHeader = (struct testHeader *)payload_end;
		testHeader->lastHop = app_conf.hops;
		testHeader->hopCount = 1;
		testHeader->old_dst = udphdr->dest;

		*len += testHeaderLen;
		udphdr->len = htons(ntohs(udphdr->len) + testHeaderLen);
		iph->tot_len = htons(ntohs(iph->tot_len) + testHeaderLen);

		udphdr->dest = htons(TEST_PORT);
	} else {
		// check if the test header is present
		if (payload_len < testHeaderLen) {
			log_error("ERROR: Test header not found in packet");
			return;
		}

		// testHeader is at the end of the UDP payload
		testHeader = (struct testHeader *)(payload_end - testHeaderLen);
		testHeader->hopCount++;
	}

	if (testHeader->lastHop == testHeader->hopCount) {
		uint8_t tmp_mac[ETH_ALEN];
		struct in_addr tmp_ip;
		unsigned short tmp_port;
		payload_len -= testHeaderLen;

		tmp_port = testHeader->old_dst;

		udphdr->dest = tmp_port;
		udphdr->len = htons(ntohs(udphdr->len) - testHeaderLen);
		*len -= testHeaderLen;

		tmp_port = udphdr->dest;
		udphdr->dest = udphdr->source;
		udphdr->source = tmp_port;

		iph->tot_len = htons(ntohs(iph->tot_len) - testHeaderLen);

		memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
		memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
		memcpy(eth->h_source, tmp_mac, ETH_ALEN);

		memcpy(&tmp_ip, &iph->saddr, sizeof(tmp_ip));
		memcpy(&iph->saddr, &iph->daddr, sizeof(tmp_ip));
		memcpy(&iph->daddr, &tmp_ip, sizeof(tmp_ip));
	}

	return;
}

struct sock_args {
	int socket_id;
	int next_size;
};

static void *socket_routine(void *arg)
{
	nfds_t nfds = 1;
	int ret, next_size;
	struct socket *xsk;
	struct xskvec *xskvecs, *sendvecs, *dropvecs;
	struct pollfd fds[1] = {};
	uint32_t i, nrecv, nsend, count, nb_frags = 0, wsend, wdrop, ndrop;
	struct sock_args *a = (struct sock_args *)arg;

	next_size = a->next_size;

	log_debug("SOCKET_ID: %d", a->socket_id);
	xsk = nf->thread[a->socket_id]->socket;

	cfg->xsk->poll_timeout = -1;

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

	fds[0].fd = xsk->fd;
	fds[0].events = POLLIN;

	count = 0;
	for (;;) {
		ret = flash__poll(cfg, xsk, fds, nfds);
		if (!(ret == 1 || ret == -2))
			continue;

		nrecv = flash__recvmsg(cfg, xsk, xskvecs, cfg->xsk->batch_size);

		for (i = 0; i < nrecv; i++) {
			if (next_size != 0) {
				xskvecs[i].options = ((count % next_size) << 16) | (xskvecs[i].options & 0xFFFF);
				count++;
			}
			char *pkt = xskvecs[i].data;

			if (!nb_frags++)
				process_packets(pkt, &xskvecs[i].len);

			if (IS_EOP_DESC(xskvecs[i].options))
				nb_frags = 0;

			if (app_conf.variable) {
				__u64 random_cycles =
					app_conf.variable_start + (rand() % (app_conf.variable_end - app_conf.variable_start));
				burn_cycles(random_cycles);

			} else {
				burn_cycles(app_conf.burn_cycles);
			}
		}

		flash__track_tx_and_drop(cfg, xsk, xskvecs, nrecv, sendvecs, &wsend, dropvecs, &wdrop);

		if (nrecv) {
			nsend = flash__sendmsg(cfg, xsk, sendvecs, wsend);
			if (nsend != wsend) {
				log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
				break;
			}
			ndrop = flash__dropmsg(cfg, xsk, dropvecs, wdrop);
			if (ndrop != wdrop) {
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

static void *worker__stats(void *conf)
{
	struct stats_conf *arg = (struct stats_conf *)conf;
	struct nf *nf = arg->nf;
	struct config *cfg = arg->cfg;

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

	cfg->app_name = "Flow Backpressure Application";
	cfg->app_options = flow_bp_options;
	cfg->done = &done;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;

	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;

	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	stats_arr = calloc(cfg->total_sockets, sizeof(struct test_stats));
	if (!stats_arr) {
		log_error("ERROR: Memory allocation failed for stats_arr");
		goto out_cfg;
	}

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
		args[i].next_size = nf->next_size;

		log_info("2_NEXT_SIZE: %d", args[i].next_size);

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
			log_error("ERROR: Unable to detach thread: %s", strerror(errno));
			goto out_args;
		}
	}

	stats_cfg.nf = nf;
	stats_cfg.cfg = cfg;

	if (pthread_create(&stats_thread, NULL, worker__stats, &stats_cfg)) {
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
		log_error("ERROR: Unable to detach thread: %s", strerror(errno));
		goto out_args;
	}

	flash__wait(cfg);

	flash__xsk_close(cfg, nf);

	return EXIT_SUCCESS;

out_args:
	done = true;
	free(args);
out_cfg_close:
	free(stats_arr);
	sleep(1);
	flash__xsk_close(cfg, nf);
out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
