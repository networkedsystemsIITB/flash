/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_UDS_H
#define __FLASH_UDS_H

#include <flash_defines.h>

#define UNIX_SOCKET_NAME "/var/run/flash/sock"
#define MAX_NUM_OF_CLIENTS 20
#define FLASH__CREATE_UMEM 1
#define FLASH__GET_UMEM 2
#define FLASH__CREATE_SOCKET 3
#define FLASH__CLOSE_CONN 4
#define FLASH__GET_THREAD_INFO 5
#define FLASH__GET_UMEM_OFFSET 6

int send_fd(int sockfd, int fd);
int start_uds_server(void);
int start_uds_client(void);
int recv_fd(int sockfd, int *_fd);
void send_cmd(int sockfd, int cmd);
void send_data(int sockfd, void *data, int size);
void recv_data(int sockfd, void *data, int size);
int recv_cmd(int sockfd);

#endif /* __FLASH_UDS_H */