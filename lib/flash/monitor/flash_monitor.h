/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_MONITOR_H
#define __FLASH_MONITOR_H

#include <sys/mman.h>
#include <flash_defines.h>

extern int unix_socket_server;

void *init_prompt(void *arg);
void cleanup_exit(void);
struct NFGroup *parse_json(const char *filename);
void free_nf_group(struct NFGroup *nf_group);
int configure_umem(struct nf_data *data, struct umem **_umem);
struct socket *create_new_socket(struct umem *umem, int nf_id);
const char *process_input(char *input);
void close_nf(struct umem *umem, int umem_id, int nf_id);

#endif /* __FLASH_MONITOR_H */
