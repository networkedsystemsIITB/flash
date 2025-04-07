/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 * 
 * Experimental data path for userspace AF_XDP chaining
 */

#include <stdlib.h>
#include <log.h>
#include <unistd.h>

#include "flash_nf.h"

static void __kick_tx(struct socket *xsk)
{
	int ret;
	ret = sendto(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, 0);

	if (ret >= 0 || errno == ENOBUFS || errno == EAGAIN || errno == EBUSY || errno == ENETDOWN)
		return;
	log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
	exit(EXIT_FAILURE);
}

static inline void __complete_tx_rx_first__us(struct config *cfg, struct socket *xsk_first, struct socket *xsk)
{
	uint32_t idx_cq = 0, idx_fq = 0;
	uint32_t completed, num_outstanding;

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

	num_outstanding = xsk->outstanding_tx > cfg->xsk->batch_size ? cfg->xsk->batch_size : xsk->outstanding_tx;

	/* Re-add completed TX buffers */
	completed = xsk_ring_cons__peek(&xsk->comp, num_outstanding, &idx_cq);
	if (completed > 0) {
		uint32_t i, ret;

		ret = xsk_ring_prod__reserve(&xsk_first->fill, completed, &idx_fq);
		while (ret != completed) {
			if (cfg->xsk->mode & FLASH__BUSY_POLL || xsk_ring_prod__needs_wakeup(&xsk_first->fill)) {
#ifdef STATS
				xsk->app_stats.fill_fail_polls++;
#endif
				recvfrom(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, NULL);
			}
			ret = xsk_ring_prod__reserve(&xsk_first->fill, completed, &idx_fq);
		}

		for (i = 0; i < completed; i++)
			*xsk_ring_prod__fill_addr(&xsk_first->fill, idx_fq++) = *xsk_ring_cons__comp_addr(&xsk->comp, idx_cq++);

		xsk_ring_prod__submit(&xsk_first->fill, completed);
		xsk_ring_cons__release(&xsk->comp, completed);
		xsk->outstanding_tx -= completed;
	}
}

static inline uint32_t __reserve_fq(struct config *cfg, struct socket *xsk, uint32_t num)
{
	uint32_t idx_fq = 0;
	uint32_t ret;

	ret = xsk_ring_prod__reserve(&xsk->fill, num, &idx_fq);
	while (ret != num) {
		if (cfg->xsk->mode & FLASH__BUSY_POLL || xsk_ring_prod__needs_wakeup(&xsk->fill)) {
#ifdef STATS
			xsk->app_stats.fill_fail_polls++;
#endif
			recvfrom(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, NULL);
		}
		ret = xsk_ring_prod__reserve(&xsk->fill, num, &idx_fq);
	}
	return idx_fq;
}

static inline uint32_t __reserve_tx_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, uint32_t num)
{
	uint32_t idx_tx = 0;
	uint32_t ret;

	ret = xsk_ring_prod__reserve(&xsk->tx, num, &idx_tx);
	while (ret != num) {
		__complete_tx_rx_first__us(cfg, xsk_first, xsk);
		if (cfg->xsk->mode & FLASH__BUSY_POLL || xsk_ring_prod__needs_wakeup(&xsk->tx)) {
#ifdef STATS
			xsk->app_stats.tx_wakeup_sendtos++;
#endif
			__kick_tx(xsk);
		}
		ret = xsk_ring_prod__reserve(&xsk->tx, num, &idx_tx);
	}
	return idx_tx;
}

static void __hex_dump(void *pkt, size_t length, uint64_t addr)
{
	const unsigned char *address = (unsigned char *)pkt;
	const unsigned char *line = address;
	size_t line_size = 32;
	unsigned char c;
	char buf[32];
	int i = 0;

	if (!DEBUG_HEXDUMP)
		return;

	sprintf(buf, "addr=%lu", addr);
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

size_t flash__recvmsg_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, struct xskmsghdr *msg)
{
	uint32_t idx_rx = 0;
	uint32_t rcvd, i, eop_cnt = 0;

	/* Only Tx currently is not supported 
        * in that scenario we need to call the following 
        * function somewhere else in the code
        */
	__complete_tx_rx_first__us(cfg, xsk_first, xsk);

	rcvd = xsk_ring_cons__peek(&xsk->rx, cfg->xsk->batch_size, &idx_rx);
	if (!rcvd) {
		if (cfg->xsk->mode & FLASH__BUSY_POLL || xsk_ring_prod__needs_wakeup(&xsk->fill)) {
#ifdef STATS
			xsk->app_stats.rx_empty_polls++;
#endif
			recvfrom(xsk->fd, NULL, 0, MSG_DONTWAIT, NULL, NULL);
		}
		return 0;
	}

	if (rcvd > cfg->xsk->batch_size) {
		log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	for (i = 0; i < rcvd; i++) {
		const struct xdp_desc *desc = xsk_ring_cons__rx_desc(&xsk->rx, idx_rx++);
		eop_cnt += IS_EOP_DESC(desc->options);
		uint64_t addr = desc->addr;
		uint32_t len = desc->len;
		uint64_t orig = addr;

		addr = xsk_umem__add_offset_to_addr(addr);
		uint64_t *pkt = xsk_umem__get_data(cfg->umem->buffer, addr);

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

size_t flash__sendmsg_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, struct xskvec **msgiov, uint32_t nsend)
{
	uint32_t i;
	uint32_t frags_done = 0, eop_cnt = 0;
	uint32_t nb_frags = 0;

	__complete_tx_rx_first__us(cfg, xsk_first, xsk);

	if (!nsend)
		return 0;

	uint32_t idx_tx = __reserve_tx_us(cfg, xsk, xsk_first, nsend);

	for (i = 0; i < nsend; i++) {
		struct xskvec *xv = msgiov[i];
		bool eop = IS_EOP_DESC(xv->options);
		uint64_t addr = xv->addr;

		uint32_t len = xv->len;
		nb_frags++;

		struct xdp_desc *tx_desc = xsk_ring_prod__tx_desc(&xsk->tx, idx_tx++);

		tx_desc->options = eop ? 0 : XDP_PKT_CONTD;
		tx_desc->addr = addr;
		tx_desc->len = len;

		__hex_dump(xv->data, xv->len, addr);

		if (eop) {
			frags_done += nb_frags;
			nb_frags = 0;
			eop_cnt++;
		}
	}
	xsk_ring_prod__submit(&xsk->tx, frags_done);
	xsk->outstanding_tx += frags_done;
#ifdef STATS
	xsk->ring_stats.tx_npkts += eop_cnt;
	xsk->ring_stats.tx_frags += nsend;
#endif
	return nsend;
}

size_t flash__dropmsg_us(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t ndrop)
{
	uint32_t i;
	uint32_t eop_cnt = 0;

	if (!ndrop)
		return 0;

	uint32_t idx_fq = __reserve_fq(cfg, xsk, ndrop);

	for (i = 0; i < ndrop; i++) {
		struct xskvec *xv = msgiov[i];
		uint64_t addr = xv->addr;

		uint64_t orig = xsk_umem__extract_addr(addr);
		eop_cnt += IS_EOP_DESC(xv->options);
		*xsk_ring_prod__fill_addr(&xsk->fill, idx_fq++) = orig;
	}

	xsk_ring_prod__submit(&xsk->fill, ndrop);
	return ndrop;
}
