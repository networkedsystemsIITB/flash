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
#define FLASH__GET_IP_ADDR 14
#define FLASH__GET_DST_IP_ADDR 15

/* UDS Control path APIs*/

/**
 * Starts a UDS server connection
 *
 * @return socket file descriptor on success, -1 on failure
 */
int flash__start_uds_server(void);

/**
 * Starts a UDS client connection to the monitor
 * 
 * @return socket file descriptor on success, -1 on failure
 */
int flash__start_uds_client(void);

/* UDS Data path APIs */

/**
 * Receive a command in the monitor
 *
 * @param sockfd The socket file descriptor
 *
 * @return The command received, or -1 on error
 */
int flash__recv_cmd(int sockfd);

/**
 * Send a command to the monitor
 * 
 * @param sockfd The socket file descriptor
 * @param cmd The command to send
 * 
 * @return The number of bytes sent, or -1 on error
 */
int flash__send_cmd(int sockfd, int cmd);

/**
 * Receive data from the monitor
 *
 * @param sockfd The socket file descriptor
 * @param data Pointer to the buffer where the received data will be stored
 * @param size Size of the data to receive in bytes
 *
 * @return The number of bytes received, or -1 on error
 */
int flash__recv_data(int sockfd, void *data, int size);

/**
 * Send data to the monitor
 *
 * @param sockfd The socket file descriptor
 * @param data Pointer to the data to send
 * @param size Size of the data in bytes
 * 
 * @return The number of bytes sent, or -1 on error
 */
int flash__send_data(int sockfd, void *data, int size);

/**
 * Receive a file descriptor from the monitor
 *
 * @param sockfd The socket file descriptor
 * @param _fd Pointer to an integer where the received file descriptor will be stored
 *
 * @return 0 on success, -1 on error
 */
int flash__recv_fd(int sockfd, int *_fd);

/**
 * Send a file descriptor from the monitor
 *
 * @param sockfd The socket file descriptor
 * @param fd The file descriptor to send
 *
 * @return 0 on success, -1 on error
 */
int flash__send_fd(int sockfd, int fd);

#endif /* __FLASH_UDS_H */
