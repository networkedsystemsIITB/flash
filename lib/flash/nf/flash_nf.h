/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_NF_H
#define __FLASH_NF_H

#include <flash_defines.h>
#include <poll.h>

#define XDP_PKT_CONTD (1 << 0)
#define IS_EOP_DESC(options) (!((options) & XDP_PKT_CONTD))

struct xskvec {
	void *data;	  /* Pointer to data. */
	uint32_t len;	  /* Length of data. */
	uint64_t addr;	  /* Original address */
	uint32_t options; /* Optional flags */
};

struct xskmsghdr {
	struct xskvec *msg_iov; /* Vector of data to send/receive into. */
	uint32_t msg_len;	/* Number of vectors */
};

extern bool done;

void flash__populate_fill_ring(struct thread **thread, int frame_size, int total_sockets, int umem_offset, int umem_scale);
void flash__configure_nf(struct nf **_nf, struct config *cfg);
void flash__xsk_close(struct config *cfg, struct nf *nf);
int flash__poll(struct socket *xsk, struct pollfd *fds, nfds_t nfds, int timeout);
size_t flash__recvmsg(struct config *cfg, struct socket *xsk, struct xskmsghdr *msg);
size_t flash__sendmsg(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t nsend);
size_t flash__dropmsg(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t ndrop);
unsigned long flash__get_nsecs(struct config *cfg);
void flash__dump_stats(struct config *cfg, struct socket *xsk);
void wait_for_cmd(struct config *cfg);
int set_nonblocking(int sockfd);

/* Experimental */
size_t flash__sendmsg_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, struct xskvec **msgiov, uint32_t nsend);
size_t flash__recvmsg_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, struct xskmsghdr *msg);
size_t flash__dropmsg_us(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t ndrop);
#endif /* __FLASH_NF_H */