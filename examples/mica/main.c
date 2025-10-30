#include <flash_nf.h>
#include <flash_params.h>
#include <flash_uds.h>

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <log.h>
#include <arpa/inet.h>

#include <linux/ip.h>
#include <linux/udp.h>

#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include "./ported-mica/hash.h"
#include "./ported-mica/mehcached.h"

////// MICA PART ///////
#define NUM_KEYS 2000
#define VALUE_SIZE 256

size_t default_keys[NUM_KEYS];
int keys_index = NUM_KEYS - 1;
char default_value[VALUE_SIZE];

struct mehcached_table table_o;
struct mehcached_table *table;

///// MICA END ///////
volatile bool done = false;
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
	int num_get_ops;
	bool sriov;
	uint8_t *dest_ether_addr_octet;
} app_conf;

// clang-format off
static const char *mica_options[] = {
	"-c <num>\tStart CPU (default: 0)",
	"-e <num>\tEnd CPU (default: 0)",
	"-s <num>\tStats CPU (default: 1)",
	"-o <num>\tFraction of MICA GET operations (default: 0.5)",
	"-S <mac>\tEnable SR-IOV mode and set dest MAC address",
	NULL
};
// clang-format on

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

static int configure(void)
{
	const size_t page_size = 1048576 * 2;
	const size_t num_numa_nodes = 1;
	const size_t num_pages_to_try = 16384;
	const size_t num_pages_to_reserve = 16384 - 2048;
	size_t alloc_overhead = sizeof(struct mehcached_item);

	mehcached_shm_init(page_size, num_numa_nodes, num_pages_to_try, num_pages_to_reserve);

	table = &table_o;
	size_t numa_nodes[] = { (size_t)-1 };
	mehcached_table_init(table, (NUM_KEYS + MEHCACHED_ITEMS_PER_BUCKET - 1) / MEHCACHED_ITEMS_PER_BUCKET, 1,
			     NUM_KEYS * /*MEHCACHED_ROUNDUP64*/ (alloc_overhead + 8 + 8), false, false, false, numa_nodes[0],
			     numa_nodes, MEHCACHED_MTH_THRESHOLD_FIFO);

	if (!table) {
		log_error("Failed to initialize MICA table");
		return -1;
	}

	memset(default_value, 'A', VALUE_SIZE - 1);
	default_value[VALUE_SIZE - 1] = '\0';

	for (size_t i = 0; i < NUM_KEYS; i++) {
		size_t key = i;
		default_keys[i] = key;

		uint64_t key_hash = hash((const uint8_t *)&key, sizeof(key));
		if (!mehcached_set(0, table, key_hash, (const uint8_t *)&key, sizeof(key), (const uint8_t *)&default_value,
				   sizeof(default_value), 0, false)) {
			log_error("Failed to set key %zu in MICA table", key);
			return -1;
		}
	}

	return 0;
}

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->num_get_ops = 0.5 * NUM_KEYS;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "c:e:s:o:S:")) != -1)
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
		case 'o':
			app_conf->num_get_ops = atof(optarg) * NUM_KEYS;
			break;
		case 'S':
			app_conf->dest_ether_addr_octet = get_mac_addr(optarg);
			app_conf->sriov = true;
			break;
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}
	return 0;
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

struct sock_args {
	int socket_id;
};

static void *socket_routine(void *arg)
{
	nfds_t nfds = 1;
	int ret;
	struct socket *xsk;
	struct xskvec *xskvecs, *sendvecs, *dropvecs;
	struct pollfd fds[1] = {};
	uint32_t i, nrecv, nsend, wdrop, wsend, ndrop;
	struct sock_args *a = (struct sock_args *)arg;

	xsk = nf->thread[a->socket_id]->socket;

	xskvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!xskvecs) {
		log_error("Failed to allocate xskvecs array");
		return NULL;
	}
	dropvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!dropvecs) {
		log_error("Failed to allocate dropvecs array");
		free(xskvecs);
		return NULL;
	}
	sendvecs = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));
	if (!sendvecs) {
		log_error("Failed to allocate sendvecs array");
		free(xskvecs);
		free(dropvecs);
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

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &xskvecs[i];
			void *pkt = xv->data;
			void *pkt_end = pkt + xv->len;
			struct in_addr tmp_ip;
			struct ethhdr *eth = pkt;

			if ((void *)(eth + 1) > pkt_end) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("Dropping packet: incomplete Ethernet header");
				continue;
			}

			if (eth->h_proto != htons(ETH_P_IP)) {
				log_error("Dropping packet: not an IP packet");
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			struct iphdr *iph = (void *)(eth + 1);
			if ((void *)(iph + 1) > pkt_end) {
				log_error("Dropping packet: incomplete IP header");
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			size_t hdrsize = iph->ihl * 4;
			/* Sanity check packet field is valid */
			if (hdrsize < sizeof(*iph)) {
				log_error("Dropping packet: invalid IP header length");
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			if (iph->protocol != IPPROTO_UDP) {
				log_error("Dropping packet: not a UDP packet");
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			/* Variable-length IPv4 header, need to use byte-based arithmetic */
			if ((void *)iph + hdrsize > pkt_end) {
				log_error("Dropping packet: incomplete IP header with options");
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			void *next = (void *)iph + (iph->ihl << 2);

			// Assuming only UDP packets are coming
			struct udphdr *udph = next;
			if ((void *)(udph + 1) > pkt_end) {
				log_error("Dropping packet: incomplete UDP header");
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}

			unsigned char *payload = (unsigned char *)(udph + 1);
			int udp_length = ntohs(udph->len);
			int payload_len = udp_length - sizeof(struct udphdr);

			const size_t key_size = sizeof(size_t);

			// if ((size_t)payload_len < key_size + VALUE_SIZE) {
			// 	log_error("Dropping packet: payload too small for key+value");
			// 	dropvecs[wdrop++] = xskvecs[i];
			// 	continue;
			// }

			// if ((void *)payload + key_size + VALUE_SIZE > pkt_end) {
			// 	log_error("Dropping packet: cannot read full key+value from payload");
			// 	dropvecs[wdrop++] = xskvecs[i];
			// 	continue;
			// }

			size_t key;
			char value[VALUE_SIZE];

			memcpy(&key, payload, sizeof(size_t));

			// use the key from the default set, ignoring the one in the packet
			keys_index = (keys_index + 1) % NUM_KEYS;
			key = default_keys[keys_index];
			// GET
			if (keys_index < app_conf.num_get_ops) {
				uint64_t key_hash = hash((const uint8_t *)&key, sizeof(key));
				size_t value_length = sizeof(value);

				if (!mehcached_get(0, table, key_hash, (const uint8_t *)&key, sizeof(key), (uint8_t *)&value,
						   &value_length, NULL, false)) {
					log_error("Failed to get key %zu from MICA table", key);
					dropvecs[wdrop++] = xskvecs[i];
					continue;
				}

				if (value_length != sizeof(value)) {
					log_error("Value length mismatch for key %zu: expected %zu, got %zu", key, sizeof(value),
						  value_length);
					dropvecs[wdrop++] = xskvecs[i];
					continue;
				}

				// send value
				memcpy(payload + sizeof(size_t), &value, VALUE_SIZE);

				app_conf.sriov ? update_dest_mac(pkt) : swap_mac_addresses(pkt);

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
				sendvecs[wsend++] = xskvecs[i];
			}

			// STORE
			else {
				memcpy(value, payload + key_size, VALUE_SIZE);
				memset(value, 'A', VALUE_SIZE - 1);
				value[VALUE_SIZE - 1] = '\0';
				uint64_t key_hash = hash((const uint8_t *)&key, sizeof(key));
				if (!mehcached_set(0, table, key_hash, (const uint8_t *)&key, sizeof(key), (const uint8_t *)&value,
						   sizeof(value), 0, true)) {
					log_error("Failed to set key %zu in MICA table", key);
					dropvecs[wdrop++] = xskvecs[i];
					continue;
				}

				dropvecs[wdrop++] = xskvecs[i];
			}
		}

		if (nrecv) {
			nsend = flash__sendmsg(cfg, xsk, sendvecs, wsend);
			ndrop = flash__dropmsg(cfg, xsk, dropvecs, wdrop);
			if (ndrop != wdrop || nsend != wsend) {
				log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
				break;
			}
		}

		if (done)
			break;
	}
	free(xskvecs);
	free(dropvecs);
	free(sendvecs);
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

	cfg->app_name = "MICA Application";
	cfg->app_options = mica_options;
	cfg->done = &done;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0) {
		log_error("ERROR: Failed to parse command line arguments");
		goto out_cfg;
	}
	if (parse_app_args(argc, argv, &app_conf, shift) < 0) {
		log_error("ERROR: Failed to parse application arguments");
		goto out_cfg;
	}
	if (flash__configure_nf(&nf, cfg) < 0) {
		log_error("ERROR: Failed to configure NF");
		goto out_cfg;
	}

	log_info("Control Plane Setup Done");

	if (configure() < 0) {
		log_error("ERROR: Failed to configure MICA");
		goto out_cfg;
	}

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
		log_error("ERROR: Unable to detach stats thread: %s\n", strerror(errno));
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