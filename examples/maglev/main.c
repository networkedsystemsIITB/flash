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

bool done = false;
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
} app_conf;

struct Args {
	int socket_id;
	int *next;
	int next_size;
};

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

static void load_services(void)
{
	send_cmd(cfg->uds_sockfd, FLASH__GET_IP_ADDR);
	recv_data(cfg->uds_sockfd, srv_addr, INET_ADDRSTRLEN);
	log_info("NF IP: %s", srv_addr);

	send_cmd(cfg->uds_sockfd, FLASH__GET_DST_IP_ADDR);
	recv_data(cfg->uds_sockfd, &nbackends, sizeof(int));
	log_info("Number of Backends: %d", nbackends);
	for (int i = 0; i < nbackends; i++) {
		recv_data(cfg->uds_sockfd, bkd_addr[i], INET_ADDRSTRLEN);
		log_info("Backend %d IP: %s", i, bkd_addr[i]);
	}

	char proto[PROTO_STRLEN];
	unsigned srv_port, bkd_port;
	uint8_t mac_addr[6];
	struct service_info *srv_info;
	struct backend_entry *bkd_entry;
	struct in_addr addr;
	int nservices = 1, service_first_free = 0;
	int *srvindex;
	struct service_entry *service_entries;
	struct backend_entry *backend_entries;
	struct hashmap srv_to_index;

	hashmap_init(&services, sizeof(struct service_id), sizeof(struct service_info), MAX_SERVICES);
	hashmap_init(&backends, sizeof(struct backend_id), sizeof(struct backend_info), MAX_BACKENDS);
	hashmap_init(&maglev_tables, sizeof(struct service_id), sizeof(struct maglev), MAX_SERVICES);

	service_entries = malloc(sizeof(struct service_entry) * nservices);
	backend_entries = malloc(sizeof(struct backend_entry) * nbackends);
	hashmap_init(&srv_to_index, sizeof(struct service_id), sizeof(int), nservices);

	mac_addr[0] = 0x11;
	mac_addr[1] = 0x22;
	mac_addr[2] = 0x33;
	mac_addr[3] = 0x44;
	mac_addr[4] = 0x55;
	mac_addr[5] = 0x66;
	// Manually add services and backends
	// Service 1: UDP from 192.168.1.1:80 to backend 192.168.1.2:8080
	// strcpy(srv_addr, "192.168.1.1"); Stored from main fn itself
	srv_port = 80;
	bkd_port = 8080;
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
		__builtin_memcpy(&bkd_entry->value.mac_addr, mac_addr, sizeof(mac_addr));

		srvindex = hashmap_lookup_elem(&srv_to_index, &bkd_entry->key.service);
		if (!srvindex) {
			struct service_entry *srv_entry = &service_entries[service_first_free];
			srv_entry->key = bkd_entry->key.service;
			srv_entry->value.backends = 0;
			srv_info = &srv_entry->value;

			if (hashmap_insert_elem(&srv_to_index, &srv_entry->key, &service_first_free) != 1) {
				fprintf(stderr, "ERROR: unable to add service index to hash map\n");
				exit(EXIT_FAILURE);
			}

			service_first_free++;
		} else {
			srv_info = &service_entries[*srvindex].value;
		}

		bkd_entry->key.index = srv_info->backends;
		srv_info->backends++;
	}

	for (int i = 0; i < nservices; i++) {
		// printf("%u, %u\n", service_entries[i].key.vaddr, (__u32)(service_entries[i].key.vport));
		if (hashmap_insert_elem(&services, &service_entries[i].key, &service_entries[i].value) != 1) {
			fprintf(stderr, "ERROR: unable to add service to hash map\n");
			exit(EXIT_FAILURE);
		}
	}

	for (int i = 0; i < nbackends; i++) {
		if (hashmap_insert_elem(&backends, &backend_entries[i].key, &backend_entries[i].value) != 1) {
			fprintf(stderr, "ERROR: unable to add backend to hash map\n");
			exit(EXIT_FAILURE);
		}
	}

	// Setting up lookup tables for nservices
	for (int i = 0; i < nservices; i++) {
		struct maglev *lookup = malloc(sizeof(struct maglev));
		uint32_t num_bkds = service_entries[i].value.backends;
		configure(lookup, num_bkds);
		if (hashmap_insert_elem(&maglev_tables, &service_entries[i].key, lookup) != 1) {
			fprintf(stderr, "ERROR: unable to add maglev table to hash map\n");
			exit(EXIT_FAILURE);
		}
	}

	printf("Added %u services and %u backends\n", nservices, nbackends);

	free(service_entries);
	free(backend_entries);
	hashmap_free(&srv_to_index);

	return;
}

static void *socket_routine(void *arg)
{
	struct Args *a = (struct Args *)arg;
	int socket_id = a->socket_id;
	int i, ret, nfds = 1, nrecv;
	struct pollfd fds[1] = {};
	struct xskmsghdr msg = {};

	msg.msg_iov = calloc(cfg->xsk->batch_size, sizeof(struct xskvec));

	fds[0].fd = nf->thread[socket_id]->socket->fd;
	fds[0].events = POLLIN;

	struct hashmap active_sessions;
	hashmap_init(&active_sessions, sizeof(struct session_id), sizeof(struct replace_info), MAX_SESSIONS);
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
			struct ethhdr *eth = pkt;
			if ((void *)(eth + 1) > pkt_end) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				log_info("INVALID PACKET Dropping packet: %d", tot_pkt_drop);
				continue;
			}

			if (eth->h_proto != htons(ETH_P_IP)) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				log_info("INVALID ETH PROTO Dropping packet: %d", tot_pkt_drop);
				continue;
			}

			struct iphdr *iph = (void *)(eth + 1);
			if ((void *)(iph + 1) > pkt_end) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				log_info("INVALID IPHDR Dropping packet: %d", tot_pkt_drop);
				continue;
			}

			void *next = (void *)iph + (iph->ihl << 2);

			uint16_t *sport, *dport, *l4check;

			switch (iph->protocol) {
			case IPPROTO_TCP:;
				struct tcphdr *tcph = next;
				if ((void *)(tcph + 1) > pkt_end) {
					drop[tot_pkt_drop++] = &msg.msg_iov[i];
					log_info("INVALID TCPHDR Dropping packet: %d", tot_pkt_drop);
					continue;
				}

				sport = &tcph->source;
				dport = &tcph->dest;
				l4check = &tcph->check;

				break;

			case IPPROTO_UDP:;
				struct udphdr *udph = next;
				if ((void *)(udph + 1) > pkt_end) {
					drop[tot_pkt_drop++] = &msg.msg_iov[i];
					log_info("INVALID UDPHDR Dropping packet: %d", tot_pkt_drop);
					continue;
				}

				sport = &udph->source;
				dport = &udph->dest;
				l4check = &udph->check;

				break;

			default:
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				log_info("DEFAULT Dropping packet: %d", tot_pkt_drop);
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
			printf("%u, %u, %u\n", srvid.vaddr, (__u32)(srvid.vport), (__u32)(srvid.proto));
			struct service_info *srvinfo = hashmap_lookup_elem(&services, &srvid);
			if (!srvinfo) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				log_info("ERROR: missing service --> DROPPING\n");
				continue;
			}

			struct backend_id bkdid = {
				.service = srvid,
				.index = ((struct maglev *)hashmap_lookup_elem(&maglev_tables, &srvid))
						 ->bkd_mapping[murmurhash(&sid, sizeof(struct session_id), 0) % MAGLEV_LOOKUP_SIZE]
			};
			struct backend_info *bkdinfo = hashmap_lookup_elem(&backends, &bkdid);
			if (!bkdinfo) {
				drop[tot_pkt_drop++] = &msg.msg_iov[i];
				log_info("ERROR: missing backend --> DROPPING\n");
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
				fprintf(stderr, "ERROR: unable to add forward session to map\n");
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
				fprintf(stderr, "ERROR: unable to add backward session to map\n");
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
			send[tot_pkt_send++] = &msg.msg_iov[i];
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
	hashmap_free(&active_sessions);
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

	load_services();

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