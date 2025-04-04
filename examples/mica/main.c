#include <flash_nf.h>
#include <flash_params.h>
#include <flash_uds.h>

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <log.h>
#include <arpa/inet.h>

#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>

#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "./ported-mica/hash.h"
#include "./ported-mica/mehcached.h"

#define IP_STRLEN 16
#define PROTO_STRLEN 4
#define IFNAME_STRLEN 256
#define MAX_VALID_SESSIONS 100

////// MICA PART ///////
#define NUM_KEYS 2000
#define VALUE_SIZE 256

size_t default_keys[NUM_KEYS];
int keys_index = 0;
char default_value[VALUE_SIZE];

struct mehcached_table table_o;
struct mehcached_table *table;
bool flag = false;

///// MICA END ///////
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

static void *configure(void)
{
	const size_t page_size = 1048576 * 2;
	const size_t num_numa_nodes = 1;
	const size_t num_pages_to_try = 16384;
	const size_t num_pages_to_reserve = 16384 - 2048;
	size_t alloc_overhead = sizeof(struct mehcached_item);

	mehcached_shm_init(page_size, num_numa_nodes, num_pages_to_try, num_pages_to_reserve);

	table = &table_o;
	size_t numa_nodes[] = { (size_t)-1 };
	// mehcached_table_init(table, 1, 1, 256, false, false, false, numa_nodes[0], numa_nodes, MEHCACHED_MTH_THRESHOLD_FIFO);
	mehcached_table_init(table, (NUM_KEYS + MEHCACHED_ITEMS_PER_BUCKET - 1) / MEHCACHED_ITEMS_PER_BUCKET, 1,
			     NUM_KEYS * /*MEHCACHED_ROUNDUP64*/ (alloc_overhead + 8 + 8), false, false, false, numa_nodes[0],
			     numa_nodes, MEHCACHED_MTH_THRESHOLD_FIFO);
	assert(table);

	memset(default_value, 'A', 255);
	default_value[255] = '\0';

	for (size_t i = 0; i < NUM_KEYS; i++) {
		size_t key = i;
		default_keys[i] = key;

		uint64_t key_hash = hash((const uint8_t *)&key, sizeof(key));
		if (!mehcached_set(0, table, key_hash, (const uint8_t *)&key, sizeof(key), (const uint8_t *)&default_value,
				   sizeof(default_value), 0, false))
			assert(false);
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

static uint16_t iph_checksum(void *vdata, size_t length)
{
	uint32_t acc = 0;
	uint16_t *data = vdata;

	// Sum all 16-bit words
	for (size_t i = 0; i < length / 2; i++) {
		acc += data[i];
	}

	// If length is odd, add remaining byte
	if (length & 1) {
		acc += ((uint8_t *)data)[length - 1] << 8;
	}

	// Fold into 16 bits
	while (acc >> 16) {
		acc = (acc & 0xFFFF) + (acc >> 16);
	}

	return ~acc;
}

static uint16_t udph_checksum(struct udphdr *udph, struct iphdr *iph, uint8_t *payload, size_t payload_len)
{
	struct pseudo_header {
		uint32_t src;
		uint32_t dst;
		uint8_t zero;
		uint8_t protocol;
		uint16_t udp_length;
	};
	struct pseudo_header pseudo_hdr = { 0 };
	pseudo_hdr.src = iph->saddr;
	pseudo_hdr.dst = iph->daddr;
	pseudo_hdr.zero = 0;
	pseudo_hdr.protocol = IPPROTO_UDP;
	pseudo_hdr.udp_length = udph->len;

	uint32_t sum = 0;
	uint16_t *ptr;

	// Add pseudo-header
	ptr = (uint16_t *)&pseudo_hdr;
	for (int i = 0; i < (int)sizeof(pseudo_hdr) / 2; i++)
		sum += ptr[i];

	// Add UDP header
	ptr = (uint16_t *)udph;
	for (int i = 0; i < (int)sizeof(struct udphdr) / 2; i++)
		sum += ptr[i];

	// Add payload
	ptr = (uint16_t *)payload;
	for (size_t i = 0; i < payload_len / 2; i++)
		sum += ptr[i];

	// Handle odd byte
	if (payload_len & 1)
		sum += ((uint8_t *)payload)[payload_len - 1] << 8;

	// Fold 32-bit sum to 16-bit
	while (sum >> 16)
		sum = (sum & 0xFFFF) + (sum >> 16);

	return ~sum;
}

static void *socket_routine(void *arg)
{
	struct Args *a = (struct Args *)arg;
	int socket_id = a->socket_id;
	// free(arg);
	log_info("SOCKET_ID: %d", socket_id);
	// static __u32 nb_frags;
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

		struct xskvec *drop[nrecv];
		unsigned int tot_pkt_drop = 0;
		struct xskvec *send[nrecv];
		unsigned int tot_pkt_send = 0;

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &msg.msg_iov[i];
			void *pkt = xv->data;
			void *pkt_end = pkt + xv->len;
			uint8_t tmp_mac[ETH_ALEN];
			struct in_addr tmp_ip;
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
			// Assuming only UDP packets are coming
			struct udphdr *udph = next;
			unsigned char *payload = (unsigned char *)(udph + 1);
			int udp_length = ntohs(udph->len);
			int payload_len = udp_length - sizeof(struct udphdr);

			size_t key;
			char value[256];

			// get key
			memcpy(&key, payload, sizeof(size_t));
			// Hardcoding so that half the packets are get, other half are store
			flag = !flag;
			key = default_keys[keys_index];
			keys_index = (keys_index + 1) % NUM_KEYS;
			// GET
			if (flag) {
				uint64_t key_hash = hash((const uint8_t *)&key, sizeof(key));
				size_t value_length = sizeof(value);

				if (mehcached_get(0, table, key_hash, (const uint8_t *)&key, sizeof(key), (uint8_t *)&value,
						  &value_length, NULL, false))
					assert(value_length == sizeof(value));

				// send value
				// memcpy(payload + sizeof(size_t), &value, 256);
				// re-configuring the pkt to send
				memcpy(tmp_mac, eth->h_dest, ETH_ALEN);
				memcpy(eth->h_dest, eth->h_source, ETH_ALEN);
				memcpy(eth->h_source, tmp_mac, ETH_ALEN);

				memcpy(&tmp_ip, &iph->saddr, sizeof(tmp_ip));
				memcpy(&iph->saddr, &iph->daddr, sizeof(tmp_ip));
				memcpy(&iph->daddr, &tmp_ip, sizeof(tmp_ip));
				// Recalculating iph checksum
				iph->check = 0; // Important: set to 0 before calculating
				iph->check = iph_checksum(iph, sizeof(struct iphdr));

				uint16_t tmp_port;
				memcpy(&tmp_port, &udph->source, sizeof(tmp_port));
				memcpy(&udph->source, &udph->dest, sizeof(tmp_port));
				memcpy(&udph->dest, &tmp_port, sizeof(tmp_port));

				// Recalculate UDP checksum
				udph->check = 0; // Must set to 0 before computing checksum
				udph->check = udph_checksum(udph, iph, payload, payload_len);
				send[tot_pkt_send++] = &msg.msg_iov[i];
			}

			// STORE
			else {
				// memcpy(value, payload + sizeof(size_t), 256);
				memset(value, 'A', 255);
				value[255] = '\0';
				uint64_t key_hash = hash((const uint8_t *)&key, sizeof(key));
				if (!mehcached_set(0, table, key_hash, (const uint8_t *)&key, sizeof(key), (const uint8_t *)&value,
						   sizeof(value), 0, true))
					assert(false);

				// send acknowledgement
				// memcpy(payload + sizeof(size_t), &value, 256);
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
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

	wait_for_cmd(cfg);

	flash__xsk_close(cfg, nf);

	return EXIT_SUCCESS;
}