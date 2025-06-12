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

bool done = false;
struct config *cfg = NULL;
struct nf *nf;
struct test_stats *stats_arr;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
}

struct testHeader {
	__u8 lastHop;
	__u8 hopCount;
	__u64 pktId;
	__u16 old_dst;
};

struct test_stats {
	__u64 pkt_count;
	__u64 even_next; // Next expected even packet ID
	__u64 odd_next;	 // Next expected odd packet ID
	__u64 pkt_dropped;
	__u64 pkt_corrupted;
	__u64 pkt_correct;
	__u64 even;
	__u64 odd;
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

static void parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->hops = 1;
	app_conf->burn_cycles = 10000000;
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
			abort();
		}
}

static void process_packets(void *data, __u32 *len, struct test_stats *stats)
{
	void *pos = data;
	void *data_end = data + *len;

	struct ethhdr *eth = (struct ethhdr *)pos;
	if ((void *)(eth + 1) > data_end) {
		stats->pkt_dropped++;
		return;
	}

	if (eth->h_proto != htons(ETH_P_IP)) {
		stats->pkt_dropped++;
		return;
	}

	pos = eth + 1;

	struct iphdr *iph = pos;
	size_t hdrsize;

	if ((void *)iph + 1 > data_end) {
		stats->pkt_dropped++;
		return;
	}

	hdrsize = iph->ihl * 4;
	/* Sanity check packet field is valid */
	if (hdrsize < sizeof(*iph)) {
		stats->pkt_dropped++;
		return;
	}

	if (iph->protocol != IPPROTO_UDP) {
		stats->pkt_dropped++;
		return;
	}

	/* Variable-length IPv4 header, need to use byte-based arithmetic */
	if (pos + hdrsize > data_end) {
		stats->pkt_dropped++;
		return;
	}

	pos += hdrsize;

	size_t payload_len;
	struct udphdr *udphdr = pos;

	if ((void *)udphdr + 1 > data_end) {
		stats->pkt_dropped++;
		return;
	}

	pos = udphdr + 1;
	payload_len = ntohs(udphdr->len) - sizeof(struct udphdr);

	size_t testHeaderLen = sizeof(struct testHeader);

	/* First NF */
	if (ntohs(udphdr->dest) != TEST_PORT) {
		// Shift the data to add the test header. Can we do this without memmove??
		memmove(pos + testHeaderLen, pos, payload_len);

		// Add test header and update the old length
		struct testHeader *testHeader = pos;
		testHeader->lastHop = app_conf.hops;
		testHeader->hopCount = 1;
		testHeader->pktId = stats->pkt_count++;
		*len += testHeaderLen;

		// update the udp header
		testHeader->old_dst = udphdr->dest;
		udphdr->dest = htons(TEST_PORT);
		udphdr->len = htons(ntohs(udphdr->len) + testHeaderLen);

		// update the ip payload length
		iph->tot_len = htons(ntohs(iph->tot_len) + testHeaderLen);
	} else {
		struct testHeader *testHeader = pos;

		testHeader->hopCount++;

		if (testHeader->pktId % 2 == 0) { // Even packet
			if (testHeader->pktId != stats->even_next) {
				if (testHeader->pktId < stats->even_next) {
					stats->pkt_corrupted++;
					stats->even_next = testHeader->pktId + 2;
				} else {
					stats->pkt_corrupted++;
					stats->even_next = testHeader->pktId + 2;
				}
			} else {
				stats->even++;
				stats->pkt_correct++;
				stats->even_next += 2;
			}
		} else { // Odd packet
			if (testHeader->pktId != stats->odd_next) {
				if (testHeader->pktId < stats->odd_next) {
					stats->pkt_corrupted++;
					stats->odd_next = testHeader->pktId + 2;
				} else {
					stats->pkt_corrupted++;
					stats->odd_next = testHeader->pktId + 2;
				}
			} else {
				stats->odd++;
				stats->pkt_correct++;
				stats->odd_next += 2;
			}
		}

		if (testHeader->lastHop == testHeader->hopCount) {
			uint8_t tmp_mac[ETH_ALEN];
			struct in_addr tmp_ip;
			unsigned short tmp_port;
			payload_len -= testHeaderLen;

			tmp_port = testHeader->old_dst;

			// Shift the data to remove the test header
			memmove(pos, pos + testHeaderLen, payload_len);

			// update the udp header
			udphdr->dest = tmp_port;
			udphdr->len = htons(ntohs(udphdr->len) - testHeaderLen);
			*len -= testHeaderLen;

			tmp_port = udphdr->dest;
			udphdr->dest = udphdr->source;
			udphdr->source = tmp_port;

			// update the ip payload length
			iph->tot_len = htons(ntohs(iph->tot_len) - testHeaderLen);

			memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
			memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
			memcpy(eth->h_source, tmp_mac, ETH_ALEN);

			memcpy(&tmp_ip, &iph->saddr, sizeof(tmp_ip));
			memcpy(&iph->saddr, &iph->daddr, sizeof(tmp_ip));
			memcpy(&iph->daddr, &tmp_ip, sizeof(tmp_ip));
		}
	}

	return;
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
	int *next = a->next;
	int next_size = a->next_size;
	log_info("SOCKET_ID: %d", socket_id);
	static __u32 nb_frags;
	int i, ret, nfds = 1, nrecv;
	struct pollfd fds[1] = {};
	struct xskmsghdr msg = {};

	log_info("2_NEXT_SIZE: %d", next_size);

	for (int i = 0; i < next_size; i++) {
		log_info("2_NEXT_ITEM_%d %d", i, next[i]);
	}

	cfg->xsk->poll_timeout = -1;

	msg.msg_iov = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));

	fds[0].fd = nf->thread[socket_id]->socket->fd;
	fds[0].events = POLLIN;

	nf->thread[socket_id]->socket->idle_fd.fd = nf->thread[socket_id]->socket->fd;
	nf->thread[socket_id]->socket->idle_fd.events = POLLIN;

	unsigned int count = 0;
	for (;;) {
		if (cfg->xsk->mode & FLASH__POLL) {
			ret = flash__oldpoll(nf->thread[socket_id]->socket, fds, nfds, cfg->xsk->poll_timeout);
			if (ret != 1)
				continue;
		}

		nrecv = flash__oldrecvmsg(cfg, nf->thread[socket_id]->socket, &msg);

		struct xskvec *send[nrecv];
		unsigned int tot_pkt_send = 0;
		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &msg.msg_iov[i];
			bool eop = IS_EOP_DESC(xv->options);

			if (next_size != 0) {
				xv->options = ((count % next_size) << 16) | (xv->options & 0xFFFF);
				count++;
			}
			char *pkt = xv->data;

			if (!nb_frags++)
				process_packets(pkt, &xv->len, &stats_arr[socket_id]);

			if (app_conf.variable) {
				__u64 random_cycles =
					app_conf.variable_start + (rand() % (app_conf.variable_end - app_conf.variable_start));
				burn_cycles(random_cycles);

			} else {
				burn_cycles(app_conf.burn_cycles);
			}

			send[tot_pkt_send++] = &msg.msg_iov[i];
			if (eop)
				nb_frags = 0;
		}

		if (nrecv) {
			ret = flash__oldsendmsg(cfg, nf->thread[socket_id]->socket, send, tot_pkt_send);
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
				printf("%-18s %'-14llu\n", "dropped", stats_arr[i].pkt_dropped);
				printf("%-18s %'-14llu\n", "corrupt", stats_arr[i].pkt_corrupted);
				printf("%-18s %'-14llu\n", "correct", stats_arr[i].pkt_correct);
				printf("%-18s %'-14llu\n", "even", stats_arr[i].even);
				printf("%-18s %'-14llu\n", "odd", stats_arr[i].odd);
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
	flash__populate_fill_ring(nf->thread, cfg->umem->frame_size, cfg->total_sockets, cfg->umem_offset, cfg->umem_scale);

	stats_arr = calloc(cfg->total_sockets, sizeof(struct test_stats));
	stats_arr->even_next = 0;
	stats_arr->odd_next = 1;

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

	flash__wait(cfg);

	flash__xsk_close(cfg, nf);

	return EXIT_SUCCESS;
}
