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
#include "hashmap.h"

#include <string.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <cjson/cJSON.h>

#define CONFIG_FILE "./examples/simple-firewall/config.json"

#define IP_STRLEN 16
#define PROTO_STRLEN 4
#define IFNAME_STRLEN 256
#define MAX_VALID_SESSIONS 100

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

struct hashmap valid_sessions_map;
struct session_id valid_sessions[MAX_VALID_SESSIONS];
int num_sessions = 0;

char load_balancer_addr[IP_STRLEN];
unsigned int load_balancer_port = 80;

static int read_json_config(void)
{
	struct in_addr addr;
	inet_aton(load_balancer_addr, &addr);
	FILE *file = fopen(CONFIG_FILE, "r");
	if (!file) {
		log_error("Failed to open file: %s", CONFIG_FILE);
		return -1;
	}

	// Get file size
	fseek(file, 0, SEEK_END);
	size_t file_size = ftell(file);
	rewind(file);

	char *json_data = (char *)malloc(file_size + 1);
	if (!json_data) {
		log_error("Memory allocation failed for JSON data");
		fclose(file);
		return -1;
	}

	size_t read_size = fread(json_data, 1, file_size, file);
	if (read_size != file_size) {
		log_error("Failed to read entire file: %s", CONFIG_FILE);
		free(json_data);
		fclose(file);
		return -1;
	}
	json_data[file_size] = '\0';

	fclose(file);

	cJSON *json = cJSON_Parse(json_data);
	free(json_data);
	if (!json) {
		log_error("Error parsing JSON");
		return -1;
	}

	cJSON *valid_src = cJSON_GetObjectItem(json, "valid_src");

	if (cJSON_IsArray(valid_src)) {
		int size = cJSON_GetArraySize(valid_src);
		num_sessions = size;
		if (num_sessions > MAX_VALID_SESSIONS) {
			log_error("Number of sessions (%d) exceeds maximum allowed (%d)", num_sessions, MAX_VALID_SESSIONS);
			cJSON_Delete(json); // Clean up
			return -1;
		}
		for (int i = 0; i < num_sessions; i++) {
			cJSON *entry = cJSON_GetArrayItem(valid_src, i);
			cJSON *src_addr = cJSON_GetObjectItem(entry, "src_addr");
			cJSON *src_port = cJSON_GetObjectItem(entry, "src_port");

			if (cJSON_IsString(src_addr) && cJSON_IsNumber(src_port)) {
				valid_sessions[i].saddr = inet_addr(src_addr->valuestring);
				valid_sessions[i].sport = htons(src_port->valueint);
				valid_sessions[i].proto = IPPROTO_UDP;
				valid_sessions[i].daddr = addr.s_addr;
				valid_sessions[i].dport = htons(load_balancer_port);
				log_info("Valid session added: %s:%d -> %s:%d (proto: %d)", src_addr->valuestring, src_port->valueint,
					 load_balancer_addr, load_balancer_port, valid_sessions[i].proto);
			}
		}
	} else {
		log_error("Error: valid_src is not a valid array");
		cJSON_Delete(json); // Clean up
		return -1;
	}

	cJSON_Delete(json); // Clean up
	return 0;
}

static int configure(void)
{
	int nbackends, ret;
	ret = flash__send_cmd(cfg->uds_sockfd, FLASH__GET_DST_IP_ADDR);
	if (ret < 0) {
		log_error("Failed to send command to UDS socket");
		return -1;
	}
	ret = flash__recv_data(cfg->uds_sockfd, &nbackends, sizeof(int));
	if (ret < 0) {
		log_error("Failed to receive data from UDS socket");
		return -1;
	}
	if (nbackends != 1) {
		log_error("Firewall is linked to %d load balancers", nbackends);
		return -1;
	}
	ret = flash__recv_data(cfg->uds_sockfd, load_balancer_addr, INET_ADDRSTRLEN);
	if (ret < 0) {
		log_error("Failed to receive data from UDS socket");
		return -1;
	}

	ret = read_json_config();
	if (ret < 0) {
		log_error("Failed to read JSON config");
		return -1;
	}

	ret = hashmap_init(&valid_sessions_map, sizeof(struct session_id), sizeof(int), MAX_VALID_SESSIONS);
	if (ret != 1) {
		log_error("ERROR: unable to initialize valid sessions hashmap");
		return -1;
	}
	for (int session_num = 0; session_num < num_sessions; session_num++) {
		struct session_id *key = &valid_sessions[session_num];
		int val = 1;
		ret = hashmap_insert_elem(&valid_sessions_map, (void *)key, (void *)&val);
		if (ret != 1) {
			log_error("ERROR: unable to add valid session to hashmap");
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
	struct xskvec *xskvecs, *sendvecs, *dropvecs;
	struct pollfd fds[1] = {};
	uint32_t i, nrecv, nsend, ndrop, wdrop, wsend;
	struct sock_args *a = (struct sock_args *)arg;

	int socket_id = a->socket_id;

	xsk = nf->thread[socket_id]->socket;
	log_info("SOCKET_ID: %d", socket_id);

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
				log_error("Packet too short for Ethernet header");
				continue;
			}

			if (eth->h_proto != htons(ETH_P_IP)) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("Unsupported Ethernet protocol");
				continue;
			}

			struct iphdr *iph = (void *)(eth + 1);
			if ((void *)(iph + 1) > pkt_end) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("Packet too short for IP header");
				continue;
			}

			void *next = (void *)iph + (iph->ihl << 2);

			uint16_t *sport, *dport;

			switch (iph->protocol) {
			case IPPROTO_TCP:;
				struct tcphdr *tcph = next;
				if ((void *)(tcph + 1) > pkt_end) {
					dropvecs[wdrop++] = xskvecs[i];
					log_error("Packet too short for TCP header");
					continue;
				}

				sport = &tcph->source;
				dport = &tcph->dest;

				break;

			case IPPROTO_UDP:;
				struct udphdr *udph = next;
				if ((void *)(udph + 1) > pkt_end) {
					dropvecs[wdrop++] = xskvecs[i];
					log_error("Packet too short for UDP header");
					continue;
				}

				sport = &udph->source;
				dport = &udph->dest;

				break;

			default:
				dropvecs[wdrop++] = xskvecs[i];
				log_error("Unsupported IP protocol: %d", iph->protocol);
				continue;
			}

			struct session_id sid = { 0 };
			sid.saddr = iph->saddr;
			sid.daddr = iph->daddr;
			sid.proto = iph->protocol;
			sid.sport = *sport;
			sid.dport = *dport;

			if (hashmap_lookup_elem(&valid_sessions_map, (void *)&sid) == NULL) {
				dropvecs[wdrop++] = xskvecs[i];
				continue;
			}
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
	hashmap_free(&valid_sessions_map);
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

	cfg->app_name = "simple-firewall";
	cfg->app_options = firewall_options;
	cfg->done = &done;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;

	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;

	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	log_info("Control Plane Setup Done");

	if (configure() < 0) {
		log_error("Error configuring the application");
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
		log_error("ERROR: Unable to set thread affinity: %s\n", strerror(errno));
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
	done = true;
	free(args);
out_cfg_close:
	sleep(1);
	flash__xsk_close(cfg, nf);
out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
