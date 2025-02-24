/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_DEFINES_H
#define __FLASH_DEFINES_H

#include <net/if.h>
#include <sys/types.h>
#include <xdp/xsk.h>
#include <errno.h>

#include <flash_list.h>

#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define NUM_FRAMES (4 * 1024)
#define BATCH_SIZE 64

#define DEBUG_HEXDUMP 0

struct xsk_config {
	__u32 bind_flags;
	__u32 xdp_flags;
	int ifqueue;
	__u32 queue_mask;
	uint32_t batch_size;
	int poll_timeout;
	bool mode__poll;
	bool mode__zero_copy;
	bool mode__busy_poll;
	bool mode__need_wakeup;
};

struct umem_config {
	void *buffer;
	int size;
	int frame_size;
	int flags;
};

struct config {
	int umemfd;
	int total_sockets;
	char ifname[IF_NAMESIZE];
	struct umem_config *umem;
	int uds_sockfd;
	int thread_count;
	int offset;
	bool is_primary;
	int n_threads;
	bool custom_xsk;
	bool reduce_cap;
	struct xsk_config *xsk;
};

struct xsk_umem_info {
	struct xsk_ring_prod fq;
	struct xsk_ring_cons cq;
	struct xsk_umem *umem;
	void *buffer;
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

struct xsk_socket {
	struct xsk_ring_cons *rx;
	struct xsk_ring_prod *tx;
	struct xsk_ctx *ctx;
	struct xsk_socket_config config;
	int fd;
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

struct sock_thread {
	int fd;
	FILE *stream;
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_ring_prod fill;
	struct xsk_ring_cons comp;
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

struct xsk_socket_info {
	struct sock_thread *threads;
	struct xsk_umem_config umem_config;
	struct xsk_socket_config xsk_config;
};

struct monitor_xsk_socket_info {
	struct sock_thread mxsk;
	struct xsk_socket *xsk;
	struct xsk_umem_info umem;
};

#endif /* __FLASH_DEFINES_H */
