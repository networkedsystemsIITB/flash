/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <log.h>

#include "flash_nf.h"

int flash__poll(struct socket *xsk, struct pollfd *fds, nfds_t nfds,
		int timeout)
{
#ifdef STATS
	xsk->app_stats.opt_polls++;
#endif
	return poll(fds, nfds, timeout);
}

static void __kick_tx(struct socket *xsk)
{
	int ret;
	ret = sendto(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, 0);

	if (ret >= 0 || errno == ENOBUFS || errno == EAGAIN || errno == EBUSY ||
	    errno == ENETDOWN)
		return;
	log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static inline void __complete_tx_rx_first(struct config *cfg,
					  struct socket *xsk)
{
	__u32 idx_cq = 0, idx_fq = 0;
	unsigned int completed, num_outstanding;

	if (!xsk->outstanding_tx)
		return;

	/**
     * In copy mode, Tx is driven by a syscall so we need to use e.g. sendto() to
     * really send the packets. In zero-copy mode we do not have to do this, since Tx
     * is driven by the NAPI loop. So as an optimization, we do not have to call
     * sendto() all the time in zero-copy mode.
     */
	if (cfg->xsk->bind_flags & XDP_COPY) {
#ifdef STATS
		xsk->app_stats.copy_tx_sendtos++;
#endif
		__kick_tx(xsk);
	}

	num_outstanding = xsk->outstanding_tx > cfg->xsk->batch_size ?
				  cfg->xsk->batch_size :
				  xsk->outstanding_tx;

	/* Re-add completed TX buffers */
	completed = xsk_ring_cons__peek(&xsk->comp, num_outstanding, &idx_cq);
	if (completed > 0) {
		unsigned int i, ret;

		ret = xsk_ring_prod__reserve(&xsk->fill, completed, &idx_fq);
		while (ret != completed) {
			if (cfg->xsk->mode__busy_poll ||
			    xsk_ring_prod__needs_wakeup(&xsk->fill)) {
#ifdef STATS
				xsk->app_stats.fill_fail_polls++;
#endif
				recvfrom(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL,
					 NULL);
			}
			ret = xsk_ring_prod__reserve(&xsk->fill, completed,
						     &idx_fq);
		}

		for (i = 0; i < completed; i++)
			*xsk_ring_prod__fill_addr(&xsk->fill, idx_fq++) =
				*xsk_ring_cons__comp_addr(&xsk->comp, idx_cq++);

		xsk_ring_prod__submit(&xsk->fill, completed);
		xsk_ring_cons__release(&xsk->comp, completed);
		xsk->outstanding_tx -= completed;
	}
}

static inline void __reserve_fq(struct config *cfg, struct socket *xsk,
				unsigned int num)
{
	__u32 idx_fq = 0;
	unsigned int ret;

	ret = xsk_ring_prod__reserve(&xsk->fill, num, &idx_fq);
	while (ret != num) {
		if (cfg->xsk->mode__busy_poll ||
		    xsk_ring_prod__needs_wakeup(&xsk->fill)) {
#ifdef STATS
			xsk->app_stats.fill_fail_polls++;
#endif
			recvfrom(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, NULL);
		}
		ret = xsk_ring_prod__reserve(&xsk->fill, num, &idx_fq);
	}
	xsk->idx_fq_bp = idx_fq;
}

static inline void __reserve_tx(struct config *cfg, struct socket *xsk,
				unsigned int num)
{
	__u32 idx_tx = 0;
	unsigned int ret;

	ret = xsk_ring_prod__reserve(&xsk->tx, num, &idx_tx);
	while (ret != num) {
		__complete_tx_rx_first(cfg, xsk);
		if (cfg->xsk->mode__busy_poll ||
		    xsk_ring_prod__needs_wakeup(&xsk->tx)) {
#ifdef STATS
			xsk->app_stats.tx_wakeup_sendtos++;
#endif
			__kick_tx(xsk);
		}
		ret = xsk_ring_prod__reserve(&xsk->tx, num, &idx_tx);
	}
	xsk->idx_tx_bp = idx_tx;
}

static void __hex_dump(void *pkt, size_t length, __u64 addr)
{
	const unsigned char *address = (unsigned char *)pkt;
	const unsigned char *line = address;
	size_t line_size = 32;
	unsigned char c;
	char buf[32];
	int i = 0;

	if (!DEBUG_HEXDUMP)
		return;

	sprintf(buf, "addr=%llu", addr);
	printf("length = %zu\n", length);
	printf("%s | ", buf);
	while (length-- > 0) {
		printf("%02X ", *address++);
		if (!(++i % line_size) || (length == 0 && i % line_size)) {
			if (length == 0) {
				while (i++ % line_size)
					printf("__ ");
			}
			printf(" | "); /* right close */
			while (line < address) {
				c = *line++;
				printf("%c", (c < 33 || c == 255) ? 0x2E : c);
			}
			printf("\n");
			if (length > 0)
				printf("%s | ", buf);
		}
	}
	printf("\n");
}

size_t flash__recvmsg(struct config *cfg, struct socket *xsk,
		      struct xskmsghdr *msg, int flags)
{
	__u32 idx_rx = 0;
	unsigned int rcvd, i, eop_cnt = 0;

	if ((flags & FLASH__RXTX) && !(flags & FLASH__NOSENDER))
		__complete_tx_rx_first(cfg, xsk);
	else {
		/* Not implemented */
	}
	rcvd = xsk_ring_cons__peek(&xsk->rx, cfg->xsk->batch_size, &idx_rx);
	if (!rcvd) {
		if (cfg->xsk->mode__busy_poll ||
		    xsk_ring_prod__needs_wakeup(&xsk->fill)) {
#ifdef STATS
			xsk->app_stats.rx_empty_polls++;
#endif
			recvfrom(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, NULL);
		}
		return 0;
	}

	/* Backpresure mechanism */
	if (flags & FLASH__BACKP && flags & FLASH__RX) {
		__reserve_fq(cfg, xsk, rcvd);
	} else if (flags & FLASH__BACKP && flags & FLASH__RXTX) {
		__reserve_tx(cfg, xsk, rcvd);
	} else {
		/* Not implemented */
	}

	if (rcvd > cfg->xsk->batch_size) {
		log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < rcvd; i++) {
		const struct xdp_desc *desc =
			xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++);
		eop_cnt += IS_EOP_DESC(desc->options);
		__u64 addr = desc->addr;
		__u32 len = desc->len;
		__u64 orig = addr;

		addr = xsk_umem__add_offset_to_addr(addr);
		__u64 *pkt = xsk_umem__get_data(cfg->umem->buffer, addr);

		// Put it in the message vector
		msg->msg_iov[i].data = pkt;
		msg->msg_iov[i].len = len;
		msg->msg_iov[i].addr = orig;
		msg->msg_iov[i].options = desc->options;

		__hex_dump(pkt, len, addr);
	}
	msg->msg_len = rcvd;

	xsk_ring_cons__release(&xsk->rx, rcvd);
#ifdef STATS
	xsk->ring_stats.rx_npkts += eop_cnt;
	xsk->ring_stats.rx_frags += rcvd;
#endif
	return rcvd;
}

size_t flash__sendmsg(struct config *cfg, struct socket *xsk,
		      struct xskmsghdr *msg, int flags)
{
	unsigned int nsend, i;
	nsend = msg->msg_len;
	__u32 frags_done = 0, eop_cnt = 0;
	__u32 nb_frags = 0;

	if (!nsend)
		return 0;

	if (!(flags & FLASH__BACKP) && (flags & FLASH__RX)) {
		__reserve_fq(cfg, xsk, msg->msg_len);
	} else if (!(flags & FLASH__BACKP) && (flags & FLASH__RXTX)) {
		__reserve_tx(cfg, xsk, msg->msg_len);
	} else {
		/* Not implemented */
	}
	__u32 idx_tx = xsk->idx_tx_bp;
	__u32 idx_fq = xsk->idx_fq_bp;

	for (i = 0; i < nsend; i++) {
		struct xskvec *xv = &msg->msg_iov[i];
		bool eop = IS_EOP_DESC(xv->options);
		__u64 addr = xv->addr;

		if (flags & FLASH__RXTX) {
			__u32 len = xv->len;
			nb_frags++;

			struct xdp_desc *tx_desc =
				xsk_ring_prod__tx_desc(&xsk->tx, idx_tx++);

			tx_desc->options = eop ? 0 : XDP_PKT_CONTD;
			tx_desc->addr = addr;
			tx_desc->len = len;

			__hex_dump(xv->data, xv->len, addr);

			if (eop) {
				frags_done += nb_frags;
				nb_frags = 0;
				eop_cnt++;
			}
		} else if (flags & FLASH__RX) {
			__u64 orig = xsk_umem__extract_addr(addr);
			eop_cnt += IS_EOP_DESC(xv->options);
			*xsk_ring_prod__fill_addr(&xsk->fill, idx_fq++) = orig;
		} else {
			/* Not implemented */
		}
	}
	if (flags & FLASH__RXTX) {
		xsk_ring_prod__submit(&xsk->tx, frags_done);
		xsk->outstanding_tx += frags_done;
#ifdef STATS
		xsk->ring_stats.tx_npkts += eop_cnt;
		xsk->ring_stats.tx_frags += nsend;
#endif
	} else if (flags & FLASH__RX) {
		xsk_ring_prod__submit(&xsk->fill, nsend);
	} else {
		/* Not implemented */
	}
	return nsend;
}
