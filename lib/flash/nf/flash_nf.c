/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <stdlib.h>

#include <flash_uds.h>
#include <log.h>

#include "flash_nf.h"

static int *get_ifqueues(__u32 queue_mask, int n_threads)
{
	log_info("QUEUE MASK: %d", queue_mask);
	int *ifqueue = calloc(n_threads, sizeof(int));
	int count = 0, thread = 0;
	while (queue_mask != 0) {
		if ((queue_mask & (1 << 0)) == 1)
			ifqueue[thread++] = count;
		queue_mask >>= 1;
		count++;
	}
	return ifqueue;
}

static int xsk_get_mmap_offsets(int fd, struct xdp_mmap_offsets *off)
{
	socklen_t optlen;
	int err;

	optlen = sizeof(*off);
	err = getsockopt(fd, SOL_XDP, XDP_MMAP_OFFSETS, off, &optlen);
	if (err)
		return err;

	if (optlen == sizeof(*off))
		return 0;

	return -EINVAL;
}

void close_uds_conn(struct config *cfg)
{
	send_cmd(cfg->uds_sockfd, FLASH__CLOSE_CONN);
	close(cfg->uds_sockfd);
}

static int *__configure(struct config *cfg)
{
	int *received_fd = calloc(cfg->n_threads, sizeof(int));
	int sockfd = start_uds_client();
	cfg->uds_sockfd = sockfd;

	int cmd;
	if (cfg->is_primary) {
		send_cmd(sockfd, FLASH__CREATE_UMEM);
		cmd = recv_cmd(sockfd);
		if (cmd != FLASH__GET_THREAD_INFO) {
			log_error("INAPPROPRIATE COMMAND!");
			exit(EXIT_FAILURE);
		} else
			send_data(sockfd, &cfg->total_sockets, sizeof(int));
		recv_fd(sockfd, &cfg->umemfd);
		log_info("RECEIVED NEW UMEM FD");
	} else {
		send_cmd(sockfd, FLASH__GET_UMEM);
		recv_fd(sockfd, &cfg->umemfd);
		log_info("RECEIVED EXISTING UMEM FD");
		recv_data(sockfd, &cfg->total_sockets, sizeof(int));
		log_info("TOTAL SOCKETS: %d", cfg->total_sockets);
	}

	int *ifqueue = get_ifqueues(cfg->xsk->queue_mask, cfg->n_threads);

	send_cmd(sockfd, FLASH__GET_FR_OFFSET);
	recv_data(sockfd, &cfg->offset, sizeof(int));
	log_info("RECEIVED FILL RING OFFSET: %d", cfg->offset);

	for (int i = 0; i < cfg->n_threads; i++) {
		send_cmd(sockfd, FLASH__CREATE_SOCKET);
		send_data(sockfd, cfg->ifname, IF_NAMESIZE);
		send_data(sockfd, ifqueue + i, sizeof(int));
		recv_fd(sockfd, received_fd + i);
	}

	return received_fd;
}

static int xsk_mmap_umem_rings(int i, struct xsk_socket_info *xsk)
{
	struct xdp_mmap_offsets off;
	void *fill_map, *comp_map, *rx_map, *tx_map;
	int fd = xsk->threads[i].fd;
	struct xsk_ring_cons *rx = &xsk->threads[i].rx;
	struct xsk_ring_prod *tx = &xsk->threads[i].tx;
	struct xsk_ring_prod *fill = &xsk->threads[i].fill;
	struct xsk_ring_cons *comp = &xsk->threads[i].comp;
	struct xsk_umem_config umem_config = xsk->umem_config;
	struct xsk_socket_config xsk_config = xsk->xsk_config;

	int err;
	if (fd < 0) {
		return -EFAULT;
	}

	if (!xsk || !(rx || tx || fill || comp))
		return -EFAULT;

	err = xsk_get_mmap_offsets(fd, &off);
	if (err)
		return -errno;

	if (fill) {
		fill_map = mmap(
			NULL,
			off.fr.desc + umem_config.fill_size * sizeof(__u64),
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
			XDP_UMEM_PGOFF_FILL_RING);
		if (fill_map == MAP_FAILED)
			return -errno;

		fill->mask = umem_config.fill_size - 1;
		fill->size = umem_config.fill_size;
		fill->producer = fill_map + off.fr.producer;
		fill->consumer = fill_map + off.fr.consumer;
		fill->flags = fill_map + off.fr.flags;
		fill->ring = fill_map + off.fr.desc;
		fill->cached_cons = umem_config.fill_size;
	}

	printf("FILL RING MAPPED NF!!!\n");

	if (comp) {
		comp_map = mmap(
			NULL,
			off.cr.desc + umem_config.comp_size * sizeof(__u64),
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, fd,
			XDP_UMEM_PGOFF_COMPLETION_RING);
		if (fill_map == MAP_FAILED) {
			err = -errno;
			goto out_mmap_comp;
		}

		comp->mask = umem_config.comp_size - 1;
		comp->size = umem_config.comp_size;
		comp->producer = comp_map + off.cr.producer;
		comp->consumer = comp_map + off.cr.consumer;
		comp->flags = comp_map + off.cr.flags;
		comp->ring = comp_map + off.cr.desc;
	}

	printf("COMP RING MAPPED NF!!!\n");

	if (rx) {
		rx_map = mmap(NULL,
			      off.rx.desc + xsk_config.rx_size *
						    sizeof(struct xdp_desc),
			      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			      fd, XDP_PGOFF_RX_RING);
		if (rx_map == MAP_FAILED) {
			err = -errno;
			goto out_mmap_rx;
		}

		rx->mask = xsk_config.rx_size - 1;
		rx->size = xsk_config.rx_size;
		rx->producer = rx_map + off.rx.producer;
		rx->consumer = rx_map + off.rx.consumer;
		rx->flags = rx_map + off.rx.flags;
		rx->ring = rx_map + off.rx.desc;
		rx->cached_prod = *rx->producer;
		rx->cached_cons = *rx->consumer;
	}

	printf("RX RING MAPPED NF!!!\n");

	if (tx) {
		tx_map = mmap(NULL,
			      off.tx.desc + xsk_config.tx_size *
						    sizeof(struct xdp_desc),
			      PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE,
			      fd, XDP_PGOFF_TX_RING);
		if (tx_map == MAP_FAILED) {
			err = -errno;
			goto out_mmap_tx;
		}

		tx->mask = xsk_config.tx_size - 1;
		tx->size = xsk_config.tx_size;
		tx->producer = tx_map + off.tx.producer;
		tx->consumer = tx_map + off.tx.consumer;
		tx->flags = tx_map + off.tx.flags;
		tx->ring = tx_map + off.tx.desc;
		tx->cached_prod = *tx->producer;
		/* cached_cons is r->size bigger than the real consumer pointer
         * See xsk_prod_nb_free
         */
		tx->cached_cons = *tx->consumer + xsk_config.tx_size;
	}

	printf("TX RING MAPPED NF!!!\n");
	return 0;

out_mmap_tx:
	munmap(rx_map,
	       off.rx.desc + xsk_config.rx_size * sizeof(struct xdp_desc));
out_mmap_rx:
	munmap(comp_map, off.rx.desc + umem_config.comp_size * sizeof(__u64));
out_mmap_comp:
	munmap(fill_map, off.fr.desc + umem_config.fill_size * sizeof(__u64));
	return err;
}

void flash__populate_fill_ring(struct xsk_socket_info *xsk, int frame_size,
			       int n_threads, int offset)
{
	int ret, i;
	int nr_frames = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)2;
	__u32 idx = 0;
	log_info("FILLING FILL RING");

	for (int t = 0; t < n_threads; t++) {
		ret = xsk_ring_prod__reserve(&xsk->threads[t].fill, nr_frames,
					     &idx);
		if (ret != nr_frames) {
			log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
		for (i = (t + offset) * nr_frames;
		     i < nr_frames * (t + offset + 1); i++) {
			*xsk_ring_prod__fill_addr(&xsk->threads[t].fill,
						  idx++) = i * frame_size;
		}
		log_info("THREAD: %d, OFFSET: %d", t, t + offset);
		xsk_ring_prod__submit(&xsk->threads[t].fill, nr_frames);
	}
}

void flash__xsk_close(struct config *cfg, struct xsk_socket_info *xsk)
{
	close_uds_conn(cfg);

	size_t desc_sz = sizeof(struct xdp_desc);
	struct xdp_mmap_offsets off;
	int err;

	if (!xsk)
		return;
	for (int i = 0; i < cfg->n_threads; i++) {
		err = xsk_get_mmap_offsets(xsk->threads[i].fd, &off);
		if (!err) {
			munmap(xsk->threads[i].rx.ring - off.rx.desc,
			       off.rx.desc + xsk->xsk_config.rx_size * desc_sz);
			munmap(xsk->threads[i].tx.ring - off.tx.desc,
			       off.tx.desc + xsk->xsk_config.tx_size * desc_sz);
			munmap(xsk->threads[i].fill.ring - off.fr.desc,
			       off.fr.desc + xsk->umem_config.fill_size *
						     sizeof(__u64));
			munmap(xsk->threads[i].comp.ring - off.cr.desc,
			       off.cr.desc + xsk->umem_config.comp_size *
						     sizeof(__u64));
		}
	}
	free(xsk);

	if (cfg->umem->buffer) {
		munmap(cfg->umem->buffer,
		       NUM_FRAMES * cfg->umem->frame_size * cfg->total_sockets);
	}

	if (cfg && cfg->umem && cfg->xsk) {
		free(cfg->umem);
		free(cfg->xsk);
		free(cfg);
	}
}

static bool xsk_page_aligned(void *buffer)
{
	unsigned long addr = (unsigned long)buffer;

	return !(addr & (getpagesize() - 1));
}

void flash__configure_nf(struct xsk_socket_info **_xsk, struct config *cfg)
{
	struct xsk_socket_info *xsk = calloc(1, sizeof(struct xsk_socket_info));
	xsk->threads = calloc(cfg->n_threads, sizeof(struct sock_thread));
	int *sockfd = __configure(cfg);

	int size = (size_t)NUM_FRAMES * (size_t)cfg->umem->frame_size *
		   (size_t)cfg->total_sockets;
	cfg->umem->buffer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED,
				 cfg->umemfd, 0);
	if (cfg->umem->buffer == MAP_FAILED) {
		log_error("ERROR: (UMEM setup) mmap failed \"%s\"\n",
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!size && !xsk_page_aligned(cfg->umem->buffer)) {
		log_error("ERROR: UMEM size is not page aligned \"%s\"\n",
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	struct xsk_umem_config umem_cfg = {
		/**
         * It is recommended that the fill ring size >= HW RX ring size +
         * AF_XDP RX ring size. Make sure you fill up the fill ring
         * with buffers at regular intervals, and you will with this setting
         * avoid allocation failures in the driver. These are usually quite
         * expensive since drivers have not been written to assume that
         * allocation failures are common. For regular sockets, kernel
         * allocated memory is used that only runs out in OOM situations
         * that should be rare.
         */
		.fill_size =
			(size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)2,
		.comp_size = (size_t)XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = cfg->umem->frame_size,
		.frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
		.flags = cfg->umem->flags
	};

	struct xsk_socket_config xsk_cfg = {
		.rx_size = (size_t)XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.tx_size = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS,
		.libbpf_flags = ((cfg->custom_xsk) || (cfg->reduce_cap)) ?
					XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD :
					0,
		.xdp_flags = cfg->xsk->xdp_flags,
		.bind_flags = cfg->xsk->bind_flags
	};

	xsk->umem_config = umem_cfg;
	xsk->xsk_config = xsk_cfg;

	for (int i = 0; i < cfg->n_threads; i++) {
		printf("SOCKET FD (Thread %d) :::: %d\n", i, sockfd[i]);
	}

	for (int i = 0; i < cfg->n_threads; i++) {
		xsk->threads[i].fd = sockfd[i];
		if (xsk_mmap_umem_rings(i, xsk) != 0) {
			log_error("ERROR: (Ring setup) mmap failed \"%s\"\n",
				  strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
	*_xsk = xsk;
}
