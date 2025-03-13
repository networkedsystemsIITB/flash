/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */
#ifndef __FLASH_DEFINES_H
#define __FLASH_DEFINES_H

#include <net/if.h>
#include <sys/types.h>
#include <xdp/xsk.h>
#include <errno.h>
#include <xdp/libxdp.h>
#include <linux/if_link.h>

#include <flash_list.h>
#include <time.h>

#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define NUM_FRAMES (4 * 1024)
#define BATCH_SIZE 64

#define FLASH__BUSY_POLL 0x1
#define FLASH__NO_NEED_WAKEUP 0x2
#define FLASH__POLL 0x4

#define DEBUG_HEXDUMP 0
#define STATS

struct xsk_config {
	__u32 bind_flags;
	__u32 xdp_flags;
	__u32 mode;
	uint32_t batch_size;
	int poll_timeout;
};

struct umem_config {
	void *buffer;
	int size;
	int frame_size;
	int flags;
};

struct config {
	int umem_fd;
	int total_sockets;
	int current_socket_count;
	char ifname[IF_NAMESIZE + 1];
	int *ifqueue;
	struct umem_config *umem;
	struct xsk_config *xsk;
	struct xsk_umem_config *umem_config;
	struct xsk_socket_config *xsk_config;
	bool custom_xsk;
	int umem_id;
	int nf_id;
	int umem_offset;
	bool frags_enabled;
#ifdef STATS
	clockid_t clock;
	int verbose;
	int stats_interval;
	int irqs_at_init;
	__u32 irq_no;
	bool app_stats;
	bool extra_stats;
#endif
};

struct xsk_umem {
	struct xsk_ring_prod *fill_save;
	struct xsk_ring_cons *comp_save;
	char *umem_area;
	struct xsk_umem_config config;
	int fd;
	int refcount;
	struct list_head ctx_list;
	bool rx_ring_setup_done;
	bool tx_ring_setup_done;
};

struct xsk_ctx {
	struct xsk_ring_prod *fill;
	struct xsk_ring_cons *comp;
	struct xsk_umem *umem;
	__u32 queue_id;
	int refcount;
	int ifindex;
	__u64 netns_cookie;
	int xsks_map_fd;
	struct list_head list;
	struct xdp_program *xdp_prog;
	int refcnt_map_fd;
	char ifname[IFNAMSIZ];
};

struct xsk_socket {
	struct xsk_ring_cons *rx;
	struct xsk_ring_prod *tx;
	struct xsk_ctx *ctx;
	struct xsk_socket_config config;
	int fd;
};

static const struct clockid_map {
	const char *name;
	clockid_t clockid;
} clockids_map[] = { { "REALTIME", CLOCK_REALTIME },
		     { "TAI", CLOCK_TAI },
		     { "BOOTTIME", CLOCK_BOOTTIME },
		     { "MONOTONIC", CLOCK_MONOTONIC },
		     { NULL } };

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
};

#ifdef STATS
struct xsk_ring_stats {
	unsigned long rx_frags;
	unsigned long rx_npkts;
	unsigned long tx_frags;
	unsigned long tx_npkts;
	unsigned long rx_dropped_npkts;
	unsigned long rx_invalid_npkts;
	unsigned long tx_invalid_npkts;
	unsigned long rx_full_npkts;
	unsigned long rx_fill_empty_npkts;
	unsigned long tx_empty_npkts;
	unsigned long prev_rx_frags;
	unsigned long prev_rx_npkts;
	unsigned long prev_tx_frags;
	unsigned long prev_tx_npkts;
	unsigned long prev_rx_dropped_npkts;
	unsigned long prev_rx_invalid_npkts;
	unsigned long prev_tx_invalid_npkts;
	unsigned long prev_rx_full_npkts;
	unsigned long prev_rx_fill_empty_npkts;
	unsigned long prev_tx_empty_npkts;
};

struct xsk_driver_stats {
	unsigned long intrs;
	unsigned long prev_intrs;
};

struct xsk_app_stats {
	unsigned long rx_empty_polls;
	unsigned long fill_fail_polls;
	unsigned long copy_tx_sendtos;
	unsigned long tx_wakeup_sendtos;
	unsigned long opt_polls;
	unsigned long prev_rx_empty_polls;
	unsigned long prev_fill_fail_polls;
	unsigned long prev_copy_tx_sendtos;
	unsigned long prev_tx_wakeup_sendtos;
	unsigned long prev_opt_polls;
};
#endif

struct socket {
	int fd;
	int ifqueue;
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_ring_prod fill;
	struct xsk_ring_cons comp;
	bool backpressure;
#ifdef STATS
	struct xsk_ring_stats ring_stats;
	struct xsk_app_stats app_stats;
	struct xsk_driver_stats drv_stats;
	unsigned long timestamp;
#endif
	uint32_t outstanding_tx;
	__u32 idx_fq_bp;
	__u32 idx_tx_bp;
};

struct thread {
	int id;
	int umem_offset;
	struct socket *socket;
	struct xsk_socket *xsk;
};

struct nf {
	int id;
	int *next;
	int next_size;
	struct thread **thread;
	bool is_up;
	int thread_count;
	int current_thread_count;
};

struct umem {
	int id;
	struct nf **nf;
	int nf_count;
	int current_nf_count;
	struct xsk_umem_info *umem_info;
	struct config *cfg;
};

struct NFGroup {
	struct umem **umem;
	int umem_count;
};

struct nf_data {
	int umem_id;
	int nf_id;
};

#endif /* __FLASH_DEFINES_H */
