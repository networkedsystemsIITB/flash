/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <unistd.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <fcntl.h>

#include <flash_uds.h>
#include <flash_common.h>
#include <log.h>

#include "flash_nf.h"

bool done;

int set_nonblocking(int sockfd)
{
	int flags = fcntl(sockfd, F_GETFL, 0);
	if (flags == -1) {
		perror("fcntl F_GETFL");
		return -1;
	}

	flags |= O_NONBLOCK; // Add the O_NONBLOCK flag
	if (fcntl(sockfd, F_SETFL, flags) == -1) {
		perror("fcntl F_SETFL");
		return -1;
	}

	return 0;
}

void wait_for_cmd(struct config *cfg)
{
	int cmd;
	set_nonblocking(cfg->uds_sockfd);

	while (!done) {
		int bytes_received = read(cfg->uds_sockfd, &cmd, sizeof(int));
		if (bytes_received < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				usleep(500000);
			} else {
				log_error("recv failed");
				break;
			}
		} else if (bytes_received == 0) {
			log_info("Server closed the connection");
			done = true;
			break;
		} else {
			log_info("Received signal from server");
			done = true;
		}
	}
}

static void close_uds_conn(struct config *cfg)
{
	send_cmd(cfg->uds_sockfd, FLASH__CLOSE_CONN);
	close(cfg->uds_sockfd);
	return;
}

static int *__configure(struct config *cfg, struct nf *nf)
{
	int uds_sockfd = start_uds_client();
	cfg->uds_sockfd = uds_sockfd;
	struct nf_data data;
	data.nf_id = cfg->nf_id;
	data.umem_id = cfg->umem_id;

	send_cmd(uds_sockfd, FLASH__GET_UMEM);
	send_data(uds_sockfd, &data, sizeof(struct nf_data));
	recv_fd(uds_sockfd, &cfg->umem_fd);
	log_info("RECEIVED EXISTING UMEM FD");

	recv_data(uds_sockfd, &cfg->total_sockets, sizeof(int));
	log_info("TOTAL SOCKETS: %d", cfg->total_sockets);

	recv_data(uds_sockfd, &cfg->umem->size, sizeof(int));
	log_info("UMEM SIZE: %d", cfg->umem->size);

	recv_data(uds_sockfd, &cfg->umem_scale, sizeof(int));
	log_info("UMEM SCALE: %d", cfg->umem_scale);

	send_cmd(uds_sockfd, FLASH__GET_UMEM_OFFSET);
	recv_data(uds_sockfd, &cfg->umem_offset, sizeof(int));
	log_info("RECEIVED umem_offset: %d", cfg->umem_offset);

	int *received_fd = (int *)calloc(cfg->total_sockets, sizeof(int));
	cfg->ifqueue = (int *)calloc(cfg->total_sockets, sizeof(int));
	for (int i = 0; i < cfg->total_sockets; i++) {
		send_cmd(uds_sockfd, FLASH__CREATE_SOCKET);
		recv_fd(uds_sockfd, received_fd + i);
		recv_data(uds_sockfd, &cfg->ifqueue[i], sizeof(int));
		log_info("RECEIVED SOCKET-%d FD-%d, binded to Queue-%d", i, received_fd[i], cfg->ifqueue[i]);
	}

	send_cmd(uds_sockfd, FLASH__GET_ROUTE_INFO);
	recv_data(uds_sockfd, &nf->next_size, sizeof(int));
	log_info("ROUTE SIZE: %d", nf->next_size);

	nf->next = (int *)calloc(nf->next_size, sizeof(int));
	recv_data(uds_sockfd, nf->next, sizeof(int) * nf->next_size);
	for (int i = 0; i < nf->next_size; i++) {
		log_info("ROUTE ITEM-%d %d", i, nf->next[i]);
	}

	send_cmd(uds_sockfd, FLASH__GET_BIND_FLAGS);
	recv_data(uds_sockfd, &cfg->xsk->bind_flags, sizeof(__u32));
	log_info("BIND_FLAGS: %d", cfg->xsk->bind_flags);

	send_cmd(uds_sockfd, FLASH__GET_XDP_FLAGS);
	recv_data(uds_sockfd, &cfg->xsk->xdp_flags, sizeof(__u32));
	log_info("XDP_FLAGS: %d", cfg->xsk->xdp_flags);

	send_cmd(uds_sockfd, FLASH__GET_MODE);
	recv_data(uds_sockfd, &cfg->xsk->mode, sizeof(__u32));
	log_info("MODE: %d", cfg->xsk->mode);

	if (cfg->xsk->mode & FLASH__POLL) {
		send_cmd(uds_sockfd, FLASH__GET_POLL_TIMEOUT);
		recv_data(uds_sockfd, &cfg->xsk->poll_timeout, sizeof(int));
		log_info("POLL_TIMEOUT: %d", cfg->xsk->poll_timeout);
	}

	send_cmd(uds_sockfd, FLASH__GET_FRAGS_ENABLED);
	recv_data(uds_sockfd, &cfg->frags_enabled, sizeof(bool));
	log_info("FRAGS_ENABLED: %d", cfg->frags_enabled);

	send_cmd(uds_sockfd, FLASH__GET_IFNAME);
	recv_data(uds_sockfd, cfg->ifname, IF_NAMESIZE + 1);
	log_info("IFNAME: %s", cfg->ifname);

	return received_fd;
}

static int xsk_mmap_umem_rings(struct socket *socket, struct xsk_umem_config umem_config, struct xsk_socket_config xsk_config)
{
	struct xdp_mmap_offsets off;
	void *fill_map, *comp_map, *rx_map, *tx_map;
	int fd = socket->fd;
	struct xsk_ring_cons *rx = &socket->rx;
	struct xsk_ring_prod *tx = &socket->tx;
	struct xsk_ring_prod *fill = &socket->fill;
	struct xsk_ring_cons *comp = &socket->comp;

	int err;
	if (fd < 0) {
		return -EFAULT;
	}

	if (!socket || !(rx || tx || fill || comp))
		return -EFAULT;

	err = xsk_get_mmap_offsets(fd, &off);
	if (err)
		return -errno;

	if (fill) {
		fill_map = mmap(NULL, off.fr.desc + umem_config.fill_size * sizeof(__u64), PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE, fd, XDP_UMEM_PGOFF_FILL_RING);
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

	if (comp) {
		comp_map = mmap(NULL, off.cr.desc + umem_config.comp_size * sizeof(__u64), PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_POPULATE, fd, XDP_UMEM_PGOFF_COMPLETION_RING);
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

	if (rx) {
		rx_map = mmap(NULL, off.rx.desc + xsk_config.rx_size * sizeof(struct xdp_desc), PROT_READ | PROT_WRITE,
			      MAP_SHARED | MAP_POPULATE, fd, XDP_PGOFF_RX_RING);
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

	if (tx) {
		tx_map = mmap(NULL, off.tx.desc + xsk_config.tx_size * sizeof(struct xdp_desc), PROT_READ | PROT_WRITE,
			      MAP_SHARED | MAP_POPULATE, fd, XDP_PGOFF_TX_RING);
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

	return 0;

out_mmap_tx:
	munmap(rx_map, off.rx.desc + xsk_config.rx_size * sizeof(struct xdp_desc));
out_mmap_rx:
	munmap(comp_map, off.rx.desc + umem_config.comp_size * sizeof(__u64));
out_mmap_comp:
	munmap(fill_map, off.fr.desc + umem_config.fill_size * sizeof(__u64));
	return err;
}

void flash__populate_fill_ring(struct thread **thread, int frame_size, int total_sockets, int umem_offset, int umem_scale)
{
	int ret, i;
	int nr_frames = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)2 * (size_t)umem_scale;
	__u32 idx = 0;

	for (int t = 0; t < total_sockets; t++) {
		ret = xsk_ring_prod__reserve(&thread[t]->socket->fill, nr_frames, &idx);
		if (ret != nr_frames) {
			log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
		for (i = (t + umem_offset) * nr_frames; i < nr_frames * (t + umem_offset + 1); i++) {
			*xsk_ring_prod__fill_addr(&thread[t]->socket->fill, idx++) = i * frame_size;
		}
		log_info("THREAD: %d, umem_offset: %d", t, t + umem_offset);
		xsk_ring_prod__submit(&thread[t]->socket->fill, nr_frames);
	}
}

void flash__xsk_close(struct config *cfg, struct nf *nf)
{
	log_info("Shutting down...");

	close_uds_conn(cfg);

	size_t desc_sz = sizeof(struct xdp_desc);
	struct xdp_mmap_offsets off;
	int err;

	if (!nf)
		return;
	for (int i = 0; i < cfg->total_sockets; i++) {
		err = xsk_get_mmap_offsets(nf->thread[i]->socket->fd, &off);
		if (!err) {
			munmap(nf->thread[i]->socket->rx.ring - off.rx.desc, off.rx.desc + cfg->xsk_config->rx_size * desc_sz);
			munmap(nf->thread[i]->socket->tx.ring - off.tx.desc, off.tx.desc + cfg->xsk_config->tx_size * desc_sz);
			munmap(nf->thread[i]->socket->fill.ring - off.fr.desc,
			       off.fr.desc + cfg->umem_config->fill_size * sizeof(__u64));
			munmap(nf->thread[i]->socket->comp.ring - off.cr.desc,
			       off.cr.desc + cfg->umem_config->comp_size * sizeof(__u64));
		}
		free(nf->thread[i]->socket);
		free(nf->thread[i]);
	}
	free(nf->thread);
	free(nf);

	if (cfg->umem->buffer) {
		munmap(cfg->umem->buffer, NUM_FRAMES * cfg->umem->frame_size * cfg->total_sockets);
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

void flash__configure_nf(struct nf **_nf, struct config *cfg)
{
	struct nf *nf = (struct nf *)calloc(1, sizeof(struct nf));
	int *sockfd = __configure(cfg, nf);

	if (cfg->total_sockets <= 0)
		log_error("Invalid number of sockets");
	nf->thread = (struct thread **)calloc(cfg->total_sockets, sizeof(struct thread *));

	for (int i = 0; i < cfg->total_sockets; i++)
		nf->thread[i] = (struct thread *)calloc(1, sizeof(struct thread));

	int size = cfg->umem->size;
	cfg->umem->buffer = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, cfg->umem_fd, 0);
	if (cfg->umem->buffer == MAP_FAILED) {
		log_error("ERROR: (UMEM setup) mmap failed \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	if (!size && !xsk_page_aligned(cfg->umem->buffer)) {
		log_error("ERROR: UMEM size is not page aligned \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	setup_xsk_config(&cfg->xsk_config, &cfg->umem_config, cfg);

	for (int i = 0; i < cfg->total_sockets; i++) {
		log_info("SOCKET FD (Thread %d) :::: %d\n", i, sockfd[i]);
	}

	for (int i = 0; i < cfg->total_sockets; i++) {
		nf->thread[i]->socket = (struct socket *)calloc(1, sizeof(struct socket));
		nf->thread[i]->socket->fd = sockfd[i];
		nf->thread[i]->socket->ifqueue = cfg->ifqueue[i];
		if (xsk_mmap_umem_rings(nf->thread[i]->socket, *cfg->umem_config, *cfg->xsk_config) != 0) {
			log_error("ERROR: (Ring setup) mmap failed \"%s\"\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
	free(sockfd);
	*_nf = nf;
}
