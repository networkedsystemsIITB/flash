/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * userspace-chain: User space chain example
 */

#include <signal.h>
#include <pthread.h>
#include <net/ethernet.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

#include "ring-benchmark.h"

// #define MPSC
// #define SPSC_OPT
// #define BP

/* Rx/Tx descriptor */
struct xdp_desc {
	__u64 addr;
	__u32 len;
	__u32 options;
};

///////////// owner ring buffer /////////////
#define struct_size(p, member, count)                                            \
	({                                                                       \
		size_t __size = sizeof(*(p)) + (count) * sizeof((p)->member[0]); \
		(__size < sizeof(*(p))) ? SIZE_MAX : __size;                     \
	})

#define is_power_of_2(n) (n != 0 && ((n & (n - 1)) == 0))

/* Global shared values */
struct owner_us_ring {
	volatile __u32 producer;
	__u32 pad1;
	volatile __u32 consumer;
	__u32 pad2;
	__u32 flags;
	__u32 pad3;
#ifdef MPSC
	volatile __u32 producer_head;
	__u32 pad4;
#endif
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

///////////// guest ring buffer /////////////

struct guest_queue {
	__u32 cached_prod;
	__u32 cached_cons;
	__u32 mask;
	__u32 size;
	volatile __u32 *producer;
	volatile __u32 *consumer;
#ifdef MPSC
	volatile __u32 *producer_head;
#endif
	void *ring;
};

///////////// guest ring buffer operations /////////////

#ifdef MPSC

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

		__atomic_thread_fence(__ATOMIC_ACQUIRE);

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
		success = __atomic_compare_exchange_n(r->producer_head, old_head, *new_head, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED);
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

#else

static inline __u32 guest_prod_nb_free(struct guest_queue *q, __u32 max)
{
	__u32 free_entries = q->size - (q->cached_prod - q->cached_cons);

	if (free_entries >= max)
		return max;

	q->cached_cons = __atomic_load_n(q->consumer, __ATOMIC_ACQUIRE);
	free_entries = q->size - (q->cached_prod - q->cached_cons);

	return free_entries >= max ? max : free_entries;
}

#ifdef SPSC_OPT

static inline __u32 guest_bulk_enqueue_rxtx(struct guest_queue *q, struct xdp_desc *descs, __u32 n_descs)
{
	__u32 n = guest_prod_nb_free(q, n_descs);

	if (n == 0)
		return 0;

	struct xdp_desc *new_descs = (struct xdp_desc *)q->ring;

	for (__u32 i = 0; i < n; i++)
		new_descs[q->cached_prod++ & q->mask] = descs[i];

	__atomic_store_n(q->producer, q->cached_prod, __ATOMIC_RELEASE);

	return n;
}

#else

static inline bool guest_prod_is_full(struct guest_queue *q)
{
	return guest_prod_nb_free(q, 1) ? false : true;
}

static inline int guest_prod_reserve_desc(struct guest_queue *q, __u64 addr, __u32 len, __u32 options)
{
	struct xdp_desc *descs = (struct xdp_desc *)q->ring;
	__u32 idx;

	if (guest_prod_is_full(q))
		return -ENOBUFS;

	idx = q->cached_prod++ & q->mask;
	descs[idx].addr = addr;
	descs[idx].len = len;
	descs[idx].options = options;

	return 0;
}

static inline void __xskq_prod_submit(struct guest_queue *q)
{
	__atomic_store_n(q->producer, q->cached_prod, __ATOMIC_RELEASE);
}

static inline __u32 guest_bulk_enqueue_rxtx(struct guest_queue *q, struct xdp_desc *descs, __u32 n_descs)
{
	__u32 idx;

	for (idx = 0; idx < n_descs; idx++) {
		if (guest_prod_reserve_desc(q, descs[idx].addr, descs[idx].len, descs[idx].options) != 0)
			break;
	}

	__xskq_prod_submit(q);

	return idx;
}

#endif

#endif

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

		return true;
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

	while (nb_pkts < max_entries && owner_peek_desc(q, &descs[nb_pkts])) {
		nb_pkts++;
	}

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

	q->ring = (struct owner_us_ring *)malloc(size);
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
	q->cached_prod = 0;
	q->cached_cons = 0;

	sleep(1);
	*queue = q;
	return 0;
}

///////////// guest ring buffer creation /////////////

static int guest_init_queue(struct owner_queue *oq, struct guest_queue **gq)
{
	struct guest_queue *q;

	if (!oq || oq->nentries == 0 || *gq || !is_power_of_2(oq->nentries))
		return -EINVAL;

	q = calloc(1, sizeof(*q));
	if (!q)
		return 1;

	q->consumer = &oq->ring->consumer;
	q->producer = &oq->ring->producer;
#ifdef MPSC
	q->producer_head = &oq->ring->producer_head;
#endif
	q->size = oq->nentries;
	q->mask = oq->ring_mask;
	q->ring = ((struct owner_rxtx_ring *)oq->ring)->desc;
	q->cached_prod = 0;
	q->cached_cons = 0;

	*gq = q;
	return 0;
}

#define RING_SIZE 4096
#define BATCH_SIZE 64
#define TOTAL_PKTS 100

static void set_affinity(int cpu)
{
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(cpu, &cpuset);

	if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) != 0) {
		fprintf(stderr, "ERROR: Unable to set thread affinity: errno %d\n", errno);
		exit(EXIT_FAILURE);
	}
}

#ifndef BP

static void *guest_thread(void *arg)
{
	set_affinity(1);

	struct guest_queue *gq = (struct guest_queue *)arg;
	struct xdp_desc descs[BATCH_SIZE];
	__u64 start, end;

	start = rdtsc_precise();

	for (__u32 num_pkts = 0; num_pkts < TOTAL_PKTS;)
		num_pkts += guest_bulk_enqueue_rxtx(gq, descs, BATCH_SIZE);

	end = rdtsc_precise();

	printf("guest enqueue: %lf ns\n", (1E9 * (end - start)) / get_timer_hz());

	return NULL;
}

static void *owner_thread(void *arg)
{
	set_affinity(2);

	struct owner_queue *oq = (struct owner_queue *)arg;
	struct xdp_desc descs[BATCH_SIZE];
	__u64 start, end;
	__u32 num_pkts = 0;

	while (num_pkts == 0) {
		start = rdtsc_precise();
		num_pkts = owner_bulk_dequeue_rxtx(oq, descs, BATCH_SIZE);
	}

	while (num_pkts < TOTAL_PKTS)
		num_pkts += owner_bulk_dequeue_rxtx(oq, descs, BATCH_SIZE);

	end = rdtsc_precise();

	printf("owner dequeue: %lf ns\n", (1E9 * (end - start)) / get_timer_hz());

	return NULL;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

#ifdef MPSC
	printf("MPSC - 2 Threads\n");
#else
	printf("SPSC - 2 Threads\n");
#endif

	get_timer_hz();

	struct owner_queue *owner_queue = NULL;
	struct guest_queue *guest_queue = NULL;

	owner_init_queue(RING_SIZE, &owner_queue);
	guest_init_queue(owner_queue, &guest_queue);

	pthread_t guest_thread_id;
	pthread_t owner_thread_id;

	pthread_create(&guest_thread_id, NULL, guest_thread, (void *)guest_queue);
	pthread_create(&owner_thread_id, NULL, owner_thread, (void *)owner_queue);

	pthread_join(guest_thread_id, NULL);
	pthread_join(owner_thread_id, NULL);

	return EXIT_SUCCESS;
}

#else

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

#ifdef MPSC
	printf("MPSC - 1 Thread\n");
#else
	printf("SPSC - 1 Thread\n");
#endif

	set_affinity(1);

	get_timer_hz();

	struct owner_queue *owner_queue = NULL;
	struct guest_queue *guest_queue = NULL;

	owner_init_queue(RING_SIZE, &owner_queue);
	guest_init_queue(owner_queue, &guest_queue);

	struct xdp_desc owner_descs[BATCH_SIZE], guest_descs[BATCH_SIZE];
	__u64 owner_start, owner_end, guest_start, guest_end;
	__u32 owner_num_pkts = 0, guest_num_pkts = 0;

	guest_start = rdtsc_precise();
	owner_start = rdtsc_precise();

	while (guest_num_pkts < TOTAL_PKTS && owner_num_pkts < TOTAL_PKTS) {
		guest_num_pkts += guest_bulk_enqueue_rxtx(guest_queue, guest_descs, BATCH_SIZE);
		owner_num_pkts += owner_bulk_dequeue_rxtx(owner_queue, owner_descs, BATCH_SIZE);
	}

	guest_end = rdtsc_precise();

	while (owner_num_pkts < TOTAL_PKTS)
		owner_num_pkts += owner_bulk_dequeue_rxtx(owner_queue, owner_descs, BATCH_SIZE);

	owner_end = rdtsc_precise();

	printf("guest enqueue: %lf ns\n", (1E9 * (guest_end - guest_start)) / get_timer_hz());
	printf("owner dequeue: %lf ns\n", (1E9 * (owner_end - owner_start)) / get_timer_hz());

	return EXIT_SUCCESS;
}

#endif
