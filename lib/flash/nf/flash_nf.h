/* SPDX-License-Identifier: Apache-2.0
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
	void *data;    /* Pointer to data. */
	__u32 len;     /* Length of data. */
	__u64 addr;    /* Original addres */
	__u32 options; /* Optional flags */
};

struct xskmsghdr {
	struct xskvec *msg_iov; /* Vector of data to send/receive into. */
	__u32 msg_len;		/* Number of vectors */
};

extern bool done;

void flash__populate_fill_ring(struct thread **thread, int frame_size, int total_sockets, int umem_offset);
void flash__configure_nf(struct nf **_nf, struct config *cfg);
void flash__xsk_close(struct config *cfg, struct nf *nf);
int flash__poll(struct socket *xsk, struct pollfd *fds, nfds_t nfds, int timeout);
size_t flash__recvmsg(struct config *cfg, struct socket *xsk, struct xskmsghdr *msg, int flags);
size_t flash__sendmsg(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, unsigned int nsend);
size_t flash__dropmsg(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, unsigned int ndrop);
unsigned long flash__get_nsecs(struct config *cfg);
void flash__dump_stats(struct config *cfg, struct socket *xsk);
void wait_for_cmd(void);

#endif /* __FLASH_NF_H */