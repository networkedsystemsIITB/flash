/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_NF_H
#define __FLASH_NF_H

#include <flash_defines.h>
#include <poll.h>

#define FLASH__RXTX (1 << 0)
#define FLASH__BACKP (1 << 5)
#define FLASH__RX (1 << 1)
#define FLASH__NOSENDER (1 << 6)

#define XDP_PKT_CONTD (1 << 0)
#define IS_EOP_DESC(options) (!((options) & XDP_PKT_CONTD))

struct xskvec {
	void *data; /* Pointer to data. */
	__u32 len; /* Length of data. */
	__u64 addr; /* Original addres */
	__u32 options; /* Optional flags */
};

struct xskmsghdr {
	struct xskvec *msg_iov; /* Vector of data to send/receive into. */
	__u32 msg_len; /* Number of vectors */
};

void close_uds_conn(struct config *cfg);
void flash__populate_fill_ring(struct xsk_socket_info *xsk, int frame_size,
			       int n_threads, int offset);
void flash__configure_nf(struct xsk_socket_info **_xsk, struct config *cfg);
void flash__xsk_close(struct config *cfg, struct xsk_socket_info *xsk);
int flash__poll(struct sock_thread *xsk, struct pollfd *fds, nfds_t nfds,
		int timeout);
size_t flash__recvmsg(struct config *cfg, struct sock_thread *xsk,
		      struct xskmsghdr *msg, int flags);
size_t flash__sendmsg(struct config *cfg, struct sock_thread *xsk,
		      struct xskmsghdr *msg, int flags);
unsigned long flash__get_nsecs(struct config *cfg);
void flash__dump_stats(struct config *cfg, struct sock_thread *xsk, int i,
		       int flags);

#endif /* __FLASH_NF_H */