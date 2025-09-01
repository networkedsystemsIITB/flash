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
#include <poll.h>
#include <linux/version.h>

#include <flash_list.h>
#include <time.h>
#include <netinet/in.h>

#define FRAME_SIZE XSK_UMEM__DEFAULT_FRAME_SIZE
#define NUM_FRAMES (4 * 1024)
#define BATCH_SIZE 64

#define FLASH__BUSY_POLL 0x1
#define FLASH__NO_NEED_WAKEUP 0x2
#define FLASH__POLL 0x4

#define DEBUG_HEXDUMP 0
#define STATS

#define MS_PER_S 1000

struct xsk_config {
	uint32_t bind_flags;
	uint32_t xdp_flags;
	uint32_t mode;
	uint32_t batch_size;
	uint32_t idle_thres;
	uint32_t bp_thres;
	int poll_timeout;
	int idle_timeout;
	int bp_timeout;
};

struct umem_config {
	void *buffer;
	int size;
	int frame_size;
	int flags;
};

struct config {
	const char *app_name;
	const char *const *app_options;
	int umem_fd;
	int uds_sockfd;
	int umem_scale;
	int total_sockets;
	int current_socket_count;
	char ifname[IF_NAMESIZE];
	int *ifqueue;
	struct umem_config *umem;
	struct xsk_config *xsk;
	struct xsk_umem_config *umem_config;
	struct xsk_socket_config *xsk_config;
	bool smart_poll;
	bool custom_xsk;
	int umem_id;
	int nf_id;
	int umem_offset;
	bool frags_enabled;
	bool rx_first;
	volatile bool *done;
#ifdef STATS
	clockid_t clock;
	int verbose;
	int stats_interval;
	int irqs_at_init;
	uint32_t irq_no;
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
	uint32_t queue_id;
	int refcount;
	int ifindex;
	uint64_t netns_cookie;
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
	size_t rx_frags;
	size_t rx_npkts;
	size_t tx_frags;
	size_t tx_npkts;
	size_t drop_npkts;
	size_t rx_dropped_npkts;
	size_t rx_invalid_npkts;
	size_t tx_invalid_npkts;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
	size_t rx_full_npkts;
	size_t rx_fill_empty_npkts;
	size_t tx_empty_npkts;
#endif
};

struct xsk_driver_stats {
	size_t intrs;
};

struct xsk_app_stats {
	size_t rx_empty_polls;
	size_t fill_fail_polls;
	size_t copy_tx_sendtos;
	size_t tx_wakeup_sendtos;
	size_t backpressure;
	size_t opt_polls;
};
#endif

struct socket {
	int fd;
	uint8_t ifqueue;
	struct xsk_ring_cons rx;
	struct xsk_ring_prod tx;
	struct xsk_ring_prod fill;
	struct xsk_ring_cons comp;
	struct pollfd idle_fd;
	bool idle;
	void *flash_pool;
	uint32_t outstanding_tx;
	uint64_t idle_timestamp;
#ifdef STATS
	struct xsk_ring_stats ring_stats;
	struct xsk_app_stats app_stats;
	struct xsk_driver_stats drv_stats;
	struct xsk_ring_stats ring_stats_prev;
	struct xsk_app_stats app_stats_prev;
	struct xsk_driver_stats drv_stats_prev;
	size_t timestamp;
#endif
};

struct thread {
	int id;
	uint8_t ifqueue;
	int umem_offset;
	struct socket *socket;
	struct xsk_socket *xsk;
};

struct nf {
	int id;
	char ip[INET_ADDRSTRLEN];
	uint16_t port;
	int *next; // To be removed
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
