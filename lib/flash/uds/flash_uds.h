/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_UDS_H
#define __FLASH_UDS_H

#include <flash_defines.h>

#define UNIX_SOCKET_DIR "/tmp/flash"
#define UNIX_SOCKET_NAME "/tmp/flash/uds.sock"
#define MAX_NUM_OF_CLIENTS 20
#define FLASH__CREATE_UMEM 1
#define FLASH__GET_UMEM 2
#define FLASH__CREATE_SOCKET 3
#define FLASH__CLOSE_CONN 4
#define FLASH__GET_THREAD_INFO 5
#define FLASH__GET_UMEM_OFFSET 6
#define FLASH__GET_ROUTE_INFO 7
#define FLASH__GET_BIND_FLAGS 8
#define FLASH__GET_XDP_FLAGS 9
#define FLASH__GET_MODE 10
#define FLASH__GET_POLL_TIMEOUT 11
#define FLASH__GET_FRAGS_ENABLED 12
#define FLASH__GET_IFNAME 13

int send_fd(int sockfd, int fd);
int start_uds_server(void);
int start_uds_client(void);
int recv_fd(int sockfd, int *_fd);
void send_cmd(int sockfd, int cmd);
void send_data(int sockfd, void *data, int size);
void recv_data(int sockfd, void *data, int size);
int recv_cmd(int sockfd);

#endif /* __FLASH_UDS_H */
