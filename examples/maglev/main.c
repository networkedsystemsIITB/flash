#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>
#include <log.h>
#include <arpa/inet.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <cjson/cJSON.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <flash_uds.h>

#include "hashmap.h"
#include "load_balancer.h"

#define PROTO_STRLEN 4

volatile bool done = false;
struct config *cfg = NULL;
struct nf *nf;

char bkd_addr[MAX_BACKENDS][INET_ADDRSTRLEN];
char srv_addr[INET_ADDRSTRLEN];
int nbackends = 0;

struct hashmap services;
struct hashmap backends;
struct hashmap maglev_tables;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	done = true;
}

struct appconf {
	int cpu_start;
	int cpu_end;
	int stats_cpu;
	int srv_port;
	int bkd_port;
	uint8_t mac_addr[6];
} app_conf;

// clang-format off
static const char *maglev_options[] = {
	"-c <num>\tStart CPU (default: 0)",
	"-e <num>\tEnd CPU (default: 0)",
	"-s <num>\tStats CPU (default: 1)",
	"-S <mac>\tSet MAC address (default: 11:22:33:44:55:66)",
	"-p <num>\tService port (default: 80)",
	"-P <num>\tBackend port (default: 80)",
	NULL
};
// clang-format on

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	// Default values
	app_conf->cpu_start = 0;
	app_conf->cpu_end = 0;
	app_conf->stats_cpu = 1;
	app_conf->srv_port = 80;
	app_conf->bkd_port = 80;

	int ethaddr[6];
	ethaddr[0] = 0x11;
	ethaddr[1] = 0x22;
	ethaddr[2] = 0x33;
	ethaddr[3] = 0x44;
	ethaddr[4] = 0x55;
	ethaddr[5] = 0x66;
	for (int i = 0; i < 6; i++)
		app_conf->mac_addr[i] = (uint8_t)ethaddr[i];

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "hc:e:s:S:p:P:")) != -1)
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
		case 'S':
			if (sscanf(optarg, "%x:%x:%x:%x:%x:%x", &ethaddr[0], &ethaddr[1], &ethaddr[2], &ethaddr[3], &ethaddr[4],
				   &ethaddr[5]) != 6) {
				log_error("Invalid MAC address format: %s", optarg);
				return -1;
			}
			for (int i = 0; i < 6; i++)
				app_conf->mac_addr[i] = (uint8_t)ethaddr[i];
			break;
		case 'p':
			app_conf->srv_port = atoi(optarg);
			break;
		case 'P':
			app_conf->bkd_port = atoi(optarg);
			break;
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}
	return 0;
}

static void configure(struct maglev *mag, int num_bkds)
{
	int *bkd_mapping = mag->bkd_mapping;
	int next[num_bkds];
	// populate each bkd first
	int permutation[num_bkds][MAGLEV_LOOKUP_SIZE];
	for (int i = 0; i < num_bkds; i++) {
		int hashval = murmurhash(&i, sizeof(int), 0);
		int offset = hashval % MAGLEV_LOOKUP_SIZE;
		int skip = hashval % (MAGLEV_LOOKUP_SIZE - 1) + 1;
		for (int j = 0; j < MAGLEV_LOOKUP_SIZE; j++) {
			permutation[i][j] = (((offset + j * skip) % MAGLEV_LOOKUP_SIZE) + MAGLEV_LOOKUP_SIZE) % MAGLEV_LOOKUP_SIZE;
		}
	}

	for (int i = 0; i < num_bkds; i++) {
		next[i] = 0;
	}

	for (int i = 0; i < MAGLEV_LOOKUP_SIZE; i++) {
		bkd_mapping[i] = -1;
	}

	uint32_t filled = 0;
	while (1) {
		for (int i = 0; i < num_bkds; i++) {
			if (next[i] >= MAGLEV_LOOKUP_SIZE)
				continue;
			int c = permutation[i][next[i]];
			while (bkd_mapping[c] >= 0) {
				next[i]++;
				if (next[i] >= MAGLEV_LOOKUP_SIZE) {
					break;
				}
				c = permutation[i][next[i]];
			}
			bkd_mapping[c] = i;
			next[i]++;
			filled++;
			if (filled == MAGLEV_LOOKUP_SIZE) {
				return;
			}
		}
		bool end = true;
		for (int i = 0; i < num_bkds; i++) {
			if (next[i] < MAGLEV_LOOKUP_SIZE) {
				end = false;
				break;
			}
		}
		if (end) {
			break;
		}
	}

	int bkd = 0;
	int bkd1 = 0, bkd2 = 0;
	for (int i = 0; i < MAGLEV_LOOKUP_SIZE; i++) {
		if (bkd_mapping[i] < 0) {
			bkd_mapping[i] = bkd;
			bkd++;
			bkd %= num_bkds;
		}
		if (bkd_mapping[i] == 0)
			bkd1++;
		else
			bkd2++;
	}
}

// Load balancer
// vaddr,vport,proto
struct service_entry {
	struct service_id key;
	struct service_info value;
};
// Bkd adr, port, IFindex, ifname
struct backend_entry {
	struct backend_id key;
	struct backend_info value;
};

static int load_services(void)
{
	int ret;
	ret = flash__send_cmd(cfg->uds_sockfd, FLASH__GET_IP_ADDR);
	if (ret < 0) {
		log_error("Failed to send command to get NF IP address");
		return -1;
	}
	ret = flash__recv_data(cfg->uds_sockfd, srv_addr, INET_ADDRSTRLEN);
	if (ret < 0) {
		log_error("Failed to receive NF IP address");
		return -1;
	}
	log_info("NF IP: %s", srv_addr);

	ret = flash__send_cmd(cfg->uds_sockfd, FLASH__GET_DST_IP_ADDR);
	if (ret < 0) {
		log_error("Failed to send command to get Backend IP addresses");
		return -1;
	}
	ret = flash__recv_data(cfg->uds_sockfd, &nbackends, sizeof(int));
	if (ret < 0) {
		log_error("Failed to receive number of backends");
		return -1;
	}
	log_info("Number of Backends: %d", nbackends);
	if (nbackends <= 0 || nbackends > MAX_BACKENDS) {
		log_error("Invalid number of backends: %d", nbackends);
		return -1;
	}
	for (int i = 0; i < nbackends; i++) {
		log_info("Receiving Backend %d IP address", i);
		ret = flash__recv_data(cfg->uds_sockfd, bkd_addr[i], INET_ADDRSTRLEN);
		if (ret < 0) {
			log_error("Failed to receive Backend IP address %d", i);
			return -1;
		}
		log_info("Backend %d IP: %s", i, bkd_addr[i]);
	}

	char proto[PROTO_STRLEN];
	unsigned srv_port, bkd_port;
	uint8_t *mac_addr;
	struct service_info *srv_info;
	struct backend_entry *bkd_entry;
	struct in_addr addr;
	int nservices = 1, service_first_free = 0;
	int *srvindex;
	struct service_entry *service_entries;
	struct backend_entry *backend_entries;
	struct hashmap srv_to_index;

	if (hashmap_init(&services, sizeof(struct service_id), sizeof(struct service_info), MAX_SERVICES) != 1) {
		log_error("ERROR: unable to initialize services hashmap");
		return -1;
	}
	if (hashmap_init(&backends, sizeof(struct backend_id), sizeof(struct backend_info), MAX_BACKENDS) != 1) {
		log_error("ERROR: unable to initialize backends hashmap");
		goto out_1;
	}
	if (hashmap_init(&maglev_tables, sizeof(struct service_id), sizeof(struct maglev), MAX_SERVICES) != 1) {
		log_error("ERROR: unable to initialize maglev tables hashmap");
		goto out_2;
	}

	service_entries = malloc(sizeof(struct service_entry) * nservices);
	if (!service_entries) {
		log_error("ERROR: unable to allocate memory for service entries");
		goto out_3;
	}
	backend_entries = malloc(sizeof(struct backend_entry) * nbackends);
	if (!backend_entries) {
		log_error("ERROR: unable to allocate memory for backend entries");
		goto out_4;
	}
	if (hashmap_init(&srv_to_index, sizeof(struct service_id), sizeof(int), nservices) != 1) {
		log_error("ERROR: unable to initialize service to index hashmap");
		goto out_5;
	}

	mac_addr = app_conf.mac_addr;
	// Manually add services and backends
	// Service 1: UDP from 192.168.1.1:80 to backend 192.168.1.2:8080
	// strcpy(srv_addr, "192.168.1.1"); Stored from main fn itself
	srv_port = app_conf.srv_port;
	bkd_port = app_conf.bkd_port;
	strcpy(proto, "UDP");
	for (int index = 0; index < nbackends; index++) {
		bkd_entry = &backend_entries[index];
		inet_aton(srv_addr, &addr);
		bkd_entry->key.service.vaddr = addr.s_addr;
		bkd_entry->key.service.vport = htons(srv_port);
		bkd_entry->key.service.proto = IPPROTO_UDP;

		inet_aton(bkd_addr[index], &addr);
		bkd_entry->value.addr = addr.s_addr;
		bkd_entry->value.port = htons(bkd_port);
		__builtin_memcpy(&bkd_entry->value.mac_addr, mac_addr, sizeof(app_conf.mac_addr));

		srvindex = hashmap_lookup_elem(&srv_to_index, &bkd_entry->key.service);
		if (!srvindex) {
			struct service_entry *srv_entry = &service_entries[service_first_free];
			srv_entry->key = bkd_entry->key.service;
			srv_entry->value.backends = 0;
			srv_info = &srv_entry->value;

			if (hashmap_insert_elem(&srv_to_index, &srv_entry->key, &service_first_free) != 1) {
				log_error("ERROR: unable to add service to service to index hashmap");
				goto out_6;
			}

			service_first_free++;
		} else {
			srv_info = &service_entries[*srvindex].value;
		}

		bkd_entry->key.index = srv_info->backends;
		srv_info->backends++;
	}

	for (int i = 0; i < nservices; i++) {
		log_info("Adding service %u:%u proto %u with %u backends", ntohl(service_entries[i].key.vaddr),
			 ntohs(service_entries[i].key.vport), service_entries[i].key.proto, service_entries[i].value.backends);
		if (hashmap_insert_elem(&services, &service_entries[i].key, &service_entries[i].value) != 1) {
			log_error("ERROR: unable to add service to hashmap");
			goto out_6;
		}
	}

	for (int i = 0; i < nbackends; i++) {
		if (hashmap_insert_elem(&backends, &backend_entries[i].key, &backend_entries[i].value) != 1) {
			log_error("ERROR: unable to add backend to hashmap\n");
			goto out_6;
		}
	}

	// Setting up lookup tables for nservices
	for (int i = 0; i < nservices; i++) {
		struct maglev *lookup = malloc(sizeof(struct maglev));
		uint32_t num_bkds = service_entries[i].value.backends;
		configure(lookup, num_bkds);
		if (hashmap_insert_elem(&maglev_tables, &service_entries[i].key, lookup) != 1) {
			log_error("ERROR: unable to add maglev table to hashmap\n");
			goto out_6;
		}
	}

	log_info("Added %d services and %d backends", nservices, nbackends);

	free(service_entries);
	free(backend_entries);
	hashmap_free(&srv_to_index);

	return 0;

out_6:
	hashmap_free(&srv_to_index);
out_5:
	free(backend_entries);
out_4:
	free(service_entries);
out_3:
	hashmap_free(&maglev_tables);
out_2:
	hashmap_free(&backends);
out_1:
	hashmap_free(&services);
	return -1;
}

struct sock_args {
	int socket_id;
	int next_size;
};

static void *socket_routine(void *arg)
{
	int ret;
	nfds_t nfds = 1;
	struct socket *xsk;
	struct xskvec *xskvecs, *dropvecs, *sendvecs;
	struct pollfd fds[1] = {};
	uint32_t i, nrecv, nsend, ndrop, wsend, wdrop;
	struct sock_args *a = (struct sock_args *)arg;

	log_debug("Socket ID: %d", a->socket_id);
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

	struct hashmap active_sessions;
	ret = hashmap_init(&active_sessions, sizeof(struct session_id), sizeof(struct replace_info), MAX_SESSIONS);
	if (ret != 1) {
		log_error("ERROR: unable to initialize active sessions hashmap");
		free(xskvecs);
		free(dropvecs);
		free(sendvecs);
		return NULL;
	}
	for (;;) {
		ret = flash__poll(cfg, xsk, fds, nfds);
		if (!(ret == 1 || ret == -2))
			continue;
		nrecv = flash__recvmsg(cfg, xsk, xskvecs, cfg->xsk->batch_size);

		wdrop = 0;
		wsend = 0;

		for (i = 0; i < nrecv; i++) {
			struct xskvec *xv = &xskvecs[i];

			void *pkt = xv->data;
			void *pkt_end = pkt + xv->len;

			struct ethhdr *eth = pkt;
			if ((void *)(eth + 1) > pkt_end) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("ERROR: invalid Ethernet frame");
				continue;
			}

			if (eth->h_proto != htons(ETH_P_IP)) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("ERROR: not an IP packet");
				continue;
			}

			struct iphdr *iph = (void *)(eth + 1);
			if ((void *)(iph + 1) > pkt_end) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("ERROR: invalid IP header");
				continue;
			}

			void *next = (void *)iph + (iph->ihl << 2);

			uint16_t *sport, *dport, *l4check;

			switch (iph->protocol) {
			case IPPROTO_TCP:;
				struct tcphdr *tcph = next;
				if ((void *)(tcph + 1) > pkt_end) {
					log_error("ERROR: invalid TCP header");
					dropvecs[wdrop++] = xskvecs[i];
					continue;
				}

				sport = &tcph->source;
				dport = &tcph->dest;
				l4check = &tcph->check;

				break;

			case IPPROTO_UDP:;
				struct udphdr *udph = next;
				if ((void *)(udph + 1) > pkt_end) {
					dropvecs[wdrop++] = xskvecs[i];
					log_error("ERROR: invalid UDP header");
					continue;
				}

				sport = &udph->source;
				dport = &udph->dest;
				l4check = &udph->check;

				break;

			default:
				dropvecs[wdrop++] = xskvecs[i];
				log_error("ERROR: not a TCP/UDP packet");
				continue;
			}

			struct session_id sid = { 0 };
			sid.saddr = iph->saddr;
			sid.daddr = iph->daddr;
			sid.proto = iph->protocol;
			sid.sport = *sport;
			sid.dport = *dport;

			/* Used for checksum insert before forward */
			uint32_t old_addr, new_addr;
			uint16_t old_port, new_port;

			/* Look for known sessions */
			struct replace_info *rep = hashmap_lookup_elem(&active_sessions, &sid);
			if (rep) {
				goto insert;
			}

			/* New session, apply load balancing logic */
			struct service_id srvid = { .vaddr = iph->daddr, .vport = *dport, .proto = iph->protocol };
			struct service_info *srvinfo = hashmap_lookup_elem(&services, &srvid);
			if (!srvinfo) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("ERROR: service not found for %u:%u proto %u --> DROPPING", ntohl(srvid.vaddr),
					  ntohs(srvid.vport), srvid.proto);
				continue;
			}

			struct backend_id bkdid = {
				.service = srvid,
				.index = ((struct maglev *)hashmap_lookup_elem(&maglev_tables, &srvid))
						 ->bkd_mapping[murmurhash(&sid, sizeof(struct session_id), 0) % MAGLEV_LOOKUP_SIZE]
			};
			struct backend_info *bkdinfo = hashmap_lookup_elem(&backends, &bkdid);
			if (!bkdinfo) {
				dropvecs[wdrop++] = xskvecs[i];
				log_error("ERROR: backend not found for service %u:%u proto %u and index %u --> DROPPING",
					  ntohl(srvid.vaddr), ntohs(srvid.vport), srvid.proto, bkdid.index);
				continue;
			}

			/* Store the forward session */
			struct replace_info fwd_rep;
			fwd_rep.dir = DIR_TO_BACKEND;
			fwd_rep.addr = bkdinfo->addr;
			fwd_rep.port = bkdinfo->port;
			fwd_rep.bkdindex = bkdid.index;
			__builtin_memcpy(fwd_rep.mac_addr, &bkdinfo->mac_addr, sizeof(fwd_rep.mac_addr));
			rep = &fwd_rep;
			if (hashmap_insert_elem(&active_sessions, &sid, &fwd_rep) != 1) {
				log_error("ERROR: unable to add forward session to map\n");
				goto insert;
			}

			/* Store the backward session */
			struct replace_info bwd_rep;
			bwd_rep.dir = DIR_TO_CLIENT;
			bwd_rep.addr = srvid.vaddr;
			bwd_rep.port = srvid.vport;
			__builtin_memcpy(&bwd_rep.mac_addr, &eth->h_source, sizeof(eth->h_source));
			sid.daddr = sid.saddr;
			sid.dport = sid.sport;
			sid.saddr = bkdinfo->addr;
			sid.sport = bkdinfo->port;
			if (hashmap_insert_elem(&active_sessions, &sid, &bwd_rep) != 1) {
				log_error("ERROR: unable to add backward session to map\n");
				goto insert;
			}

insert:
			if (rep->dir == DIR_TO_BACKEND) {
				old_addr = iph->daddr;
				iph->daddr = rep->addr;
				old_port = *dport;
				*dport = rep->port;
			} else {
				old_addr = iph->saddr;
				iph->saddr = rep->addr;
				old_port = *sport;
				*sport = rep->port;
			}
			new_addr = rep->addr;
			new_port = rep->port;
			__builtin_memcpy(&eth->h_source, &eth->h_dest, sizeof(eth->h_source));
			__builtin_memcpy(&eth->h_dest, &rep->mac_addr, sizeof(eth->h_dest));

			/* insert ip checksum */
			uint32_t csum = ~csum_unfold(iph->check);
			csum = csum_add(csum, ~old_addr);
			csum = csum_add(csum, new_addr);
			iph->check = csum_fold(csum);

			/* insert l4 checksum */
			csum = ~csum_unfold(*l4check);
			csum = csum_add(csum, ~old_addr);
			csum = csum_add(csum, new_addr);
			csum = csum_add(csum, ~old_port);
			csum = csum_add(csum, new_port);
			*l4check = csum_fold(csum);

			xv->options = (rep->bkdindex << 16) | (xv->options & 0xFFFF);
			sendvecs[wsend++] = xskvecs[i];
		}

		if (nrecv) {
			ndrop = flash__dropmsg(cfg, xsk, dropvecs, wdrop);
			nsend = flash__sendmsg(cfg, xsk, sendvecs, wsend);
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
	hashmap_free(&active_sessions);
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

	cfg->app_name = "maglev";
	cfg->app_options = maglev_options;
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

	if (load_services() < 0) {
		log_error("ERROR: Failed to load services");
		goto out_cfg_close;
	}

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	log_info("STARTING Data Path");

	args = calloc(cfg->total_sockets, sizeof(struct sock_args));
	if (!args) {
		log_error("ERROR: Memory allocation failed for socket args");
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