/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_MONITOR_H
#define __FLASH_MONITOR_H

#include <sys/mman.h>
#include <flash_defines.h>

extern int unix_socket_server;

struct monitor_xsk_socket_info *flash__setup_xsk(struct config *mcfg,
						 struct xsk_umem_info *umem);
struct xsk_umem_info *flash__setup_umem(struct config *mcfg);
int create_new_umem(struct config **_cfg, struct xsk_umem_info **_umem,
		    int total_sockets);
struct xsk_socket *create_new_socket(struct config *cfg,
				     struct xsk_umem_info *umem, int msgsock);

#endif /* __FLASH_MONITOR_H */