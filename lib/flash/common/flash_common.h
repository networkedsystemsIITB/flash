#ifndef __FLASH_COMMON_H
#define __FLASH_COMMON_H

#include <stdlib.h>
#include <log.h>
#include <sys/socket.h>

#include <flash_defines.h>

int xsk_get_mmap_offsets(int fd, struct xdp_mmap_offsets *off);
void setup_xsk_config(struct xsk_socket_config **_xsk_config, struct xsk_umem_config **_umem_config, struct config *cfg);

#endif /* __FLASH_COMMON_H */
