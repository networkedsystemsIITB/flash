/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * userspace-chain: User space chain example
 */

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <locale.h>
#include <stdlib.h>
#include <stddef.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

bool done = false;
struct config *cfg = NULL;
struct nf *nf;

#define FLASH_MAX_SOCKETS 8

///////////// owner ring buffer /////////////
#define struct_size(p, member, count)                                            \
	({                                                                       \
		size_t __size = sizeof(*(p)) + (count) * sizeof((p)->member[0]); \
		(__size < sizeof(*(p))) ? SIZE_MAX : __size;                     \
	})

#define is_power_of_2(n) (n != 0 && ((n & (n - 1)) == 0))

/* Global shared values */
struct owner_us_ring {
	__u32 producer;
	__u32 consumer;
	__u32 flags;
	__u32 pad3;
	__u32 producer_head;
	__u32 pad4;
};

struct owner_rxtx_ring {
	struct owner_us_ring ptrs;
	struct xdp_desc desc[];
};

struct owner_queue {
	__u32 ring_mask;
	__u32 nentries;
	__u32 cached_prod;
	__u32 cached_cons;
	struct owner_us_ring *ring;
};

// Pointer to store array of owner queues it is equal to number of sockets - 1D array
struct owner_queue *owner_queues[FLASH_MAX_SOCKETS];

///////////// guest ring buffer /////////////

struct guest_queue {
	__u32 cached_prod;
	__u32 cached_cons;
	__u32 mask;
	__u32 size;
	__u32 *producer;
	__u32 *consumer;
	__u32 *producer_head;
	void *ring;
};

// Pointer to store array of guest queues - 2D array
struct guest_queue *guest_queues[FLASH_MAX_SOCKETS][FLASH_MAX_SOCKETS];

///////////// guest ring buffer operations /////////////

#define guest_cpu_relax()                               \
	do {                                            \
		asm volatile("pause\n" : : : "memory"); \
	} while (0)

static inline __u32 guest_move_prod_head(struct guest_queue *r, __u32 n, __u32 *old_head, __u32 *new_head)
{
	const __u32 capacity = r->size;
	__u32 max = n;
	int success;
	__u32 free_entries;
	__u32 cons;

	*old_head = __atomic_load_n(r->producer_head, __ATOMIC_RELAXED);

	do {
		/* Reset n to the initial burst count */
		n = max;

		cons = __atomic_load_n(r->consumer, __ATOMIC_ACQUIRE);
		/*
		 *  The subtraction is done between two unsigned 32bits value
		 * (the result is always modulo 32 bits even if we have
		 * *old_head > cons_tail). So 'free_entries' is always between 0
		 * and capacity (which is < size).
		 */
		free_entries = (capacity + cons - *old_head);

		/* check that we have enough room in ring */
		if (n > free_entries)
			n = free_entries;

		if (n == 0)
			return 0;

		*new_head = *old_head + n;
		success = __atomic_compare_exchange_n(r->producer_head, old_head, new_head, 0, __ATOMIC_ACQUIRE, __ATOMIC_ACQUIRE);
	} while (success == 0);
	return n;
}

static void guest_move_prod_tail(struct guest_queue *ring, __u32 old_val, __u32 new_val)
{
	while (__atomic_load_n(ring->producer, __ATOMIC_RELAXED) != old_val) {
		guest_cpu_relax();
	}

	__atomic_store_n(ring->producer, new_val, __ATOMIC_RELEASE);
}

static inline __u32 guest_bulk_enqueue_rxtx(struct guest_queue *q, struct xdp_desc *descs, __u32 n_descs)
{
	__u32 prod_head;
	__u32 prod_next;

	__u32 idx;

	__u32 n = guest_move_prod_head(q, n_descs, &prod_head, &prod_next);

	/* Ring full */
	if (n == 0)
		return 0;

	struct xdp_desc *new_descs = (struct xdp_desc *)q->ring;

	idx = prod_head & q->mask;
	for (__u32 i = 0; i < n; i++) {
		new_descs[idx] = descs[i];
		idx = ((idx + 1) & q->mask);
	}

	guest_move_prod_tail(q, prod_head, prod_next);

	return n;
}

///////////// owner ring buffer operations /////////////

static inline void __owner_cons_release(struct owner_queue *q)
{
	__atomic_store_n(&q->ring->consumer, q->cached_cons, __ATOMIC_RELEASE);
}

static inline void __owner_cons_peek(struct owner_queue *q)
{
	/* Refresh the local pointer */
	q->cached_prod = __atomic_load_n(&q->ring->producer, __ATOMIC_ACQUIRE);
}

static inline void owner_cons_get_entries(struct owner_queue *q)
{
	__owner_cons_release(q);
	__owner_cons_peek(q);
}

static inline bool owner_cons_read_desc(struct owner_queue *q, struct xdp_desc *desc)
{
	if (q->cached_cons != q->cached_prod) {
		struct owner_rxtx_ring *ring = (struct owner_rxtx_ring *)q->ring;
		__u32 idx = q->cached_cons & q->ring_mask;

		*desc = ring->desc[idx];
	}
	return false;
}

static inline bool owner_cons_peek_desc(struct owner_queue *q, struct xdp_desc *desc)
{
	if (q->cached_prod == q->cached_cons)
		owner_cons_get_entries(q);
	return owner_cons_read_desc(q, desc);
}

static inline void owner_cons_release(struct owner_queue *q)
{
	q->cached_cons++;
}

static bool owner_peek_desc(struct owner_queue *q, struct xdp_desc *desc)
{
	if (owner_cons_peek_desc(q, desc)) {
		owner_cons_release(q);
		return true;
	}

	return false;
}

static inline __u32 owner_bulk_dequeue_rxtx(struct owner_queue *q, struct xdp_desc *descs, __u32 max_entries)
{
	__u32 nb_pkts = 0;

	while (nb_pkts < max_entries && owner_peek_desc(q, &descs[nb_pkts]))
		nb_pkts++;

	__owner_cons_release(q);
	return nb_pkts;
}

///////////// owner ring buffer creation /////////////

static size_t owner_get_ring_size(struct owner_queue *q)
{
	struct owner_rxtx_ring *rxtx_ring;

	return struct_size(rxtx_ring, desc, q->nentries);
}

static struct owner_queue *ownerq_create(__u32 nentries)
{
	struct owner_queue *q;
	size_t size;

	q = calloc(1, sizeof(*q));
	if (!q)
		return NULL;

	q->nentries = nentries;
	q->ring_mask = nentries - 1;

	size = owner_get_ring_size(q);

	/* size which is overflowing or close to SIZE_MAX will become 0 in
	 * PAGE_ALIGN(), checking SIZE_MAX is enough due to the previous
	 * is_power_of_2(), the rest will be handled by vmalloc_user()
	 */
	if (size == SIZE_MAX) {
		free(q);
		return NULL;
	}

	q->ring = malloc(size);
	if (!q->ring) {
		free(q);
		return NULL;
	}

	return q;
}

static int owner_init_queue(__u32 entries, struct owner_queue **queue)
{
	struct owner_queue *q;

	if (entries == 0 || *queue || !is_power_of_2(entries))
		return -EINVAL;

	q = ownerq_create(entries);
	if (!q)
		return -ENOMEM;

	sleep(1);
	*queue = q;
	return 0;
}

///////////// guest ring buffer creation /////////////

static int guest_init_queue(struct owner_queue *oq, struct guest_queue **gq)
{
	struct guest_queue *q;

	if (oq || oq->nentries == 0 || *gq || !is_power_of_2(oq->nentries))
		return -EINVAL;

	q = calloc(1, sizeof(*q));
	if (!q)
		return 1;

	q->consumer = &oq->ring->consumer;
	q->producer = &oq->ring->producer;
	q->producer_head = &oq->ring->producer_head;
	q->size = oq->nentries;
	q->mask = oq->ring_mask;
	q->ring = oq->ring + offsetof(struct owner_rxtx_ring, desc);

	sleep(1);
	*gq = q;
	return 0;
}

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
	flash__populate_fill_ring(nf->thread, cfg->umem->frame_size, cfg->total_sockets, cfg->umem_offset, cfg->umem_scale);

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

		// setting up the owner queue
		if (owner_init_queue(cfg->umem->frame_size, &owner_queues[i]) != 0) {
			log_error("Error creating owner queue");
			exit(EXIT_FAILURE);
		}

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

	// Setting up the guest queue
	for (int i = 0; i < cfg->total_sockets; i++) {
		for (int j = 0; j < cfg->total_sockets; j++) {
			if (guest_init_queue(owner_queues[i], &guest_queues[i][j]) != 0) {
				log_error("Error creating guest queue");
				exit(EXIT_FAILURE);
			}
		}
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
