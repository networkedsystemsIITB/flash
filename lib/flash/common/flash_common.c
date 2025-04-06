/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <log.h>
#include <sys/socket.h>

#include <flash_defines.h>
#include "flash_common.h"

int xsk_get_mmap_offsets(int fd, struct xdp_mmap_offsets *off)
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

void setup_xsk_config(struct xsk_socket_config **_xsk_config, struct xsk_umem_config **_umem_config, struct config *cfg)
{
	log_info("SETTING XSK_CONFIG");
	struct xsk_socket_config *xsk_config = calloc(1, sizeof(struct xsk_socket_config));
	xsk_config->rx_size = (size_t)XSK_RING_CONS__DEFAULT_NUM_DESCS * (size_t)cfg->umem_scale;
	xsk_config->tx_size = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)cfg->umem_scale;
	xsk_config->libbpf_flags = cfg->custom_xsk ? XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD : 0;
	xsk_config->xdp_flags = cfg->xsk->xdp_flags;
	xsk_config->bind_flags = cfg->xsk->bind_flags;
	*_xsk_config = xsk_config;

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
	struct xsk_umem_config *umem_config = calloc(1, sizeof(struct xsk_umem_config));
	umem_config->fill_size = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)2 * (size_t)cfg->umem_scale;
	umem_config->comp_size = (size_t)XSK_RING_CONS__DEFAULT_NUM_DESCS * (size_t)cfg->umem_scale;
	umem_config->frame_size = cfg->umem->frame_size;
	umem_config->frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM;
	umem_config->flags = cfg->umem->flags;
	*_umem_config = umem_config;
}
