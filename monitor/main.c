/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <pthread.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <stdlib.h>

#include <flash_monitor.h>
#include <flash_uds.h>
#include <flash_params.h>
#include <log.h>

int umemfd = -1;
int offset = 0;
struct xsk_umem_info *umem;
struct config *cfg;

void *handle_requests(void *arg)
{
	int msgsock = *(int *)arg, cmd = -1;
	bool is_primary = false;
	struct xsk_socket **sockets = NULL;
	int total_sockets, socket_count = 0;

	do {
		log_info("Waiting for command...");
		cmd = recv_cmd(msgsock);

		switch (cmd) {
		case FLASH__CREATE_UMEM:
			send_cmd(msgsock, FLASH__GET_THREAD_INFO);
			recv_data(msgsock, &total_sockets, sizeof(int));
			umemfd = create_new_umem(&cfg, &umem, total_sockets);
			sockets = calloc(total_sockets,
					 sizeof(struct xsk_socket));
			send_fd(msgsock, umemfd);
			is_primary = true;
			break;

		case FLASH__GET_UMEM:
			send_fd(msgsock, umemfd);
			send_data(msgsock, &cfg->total_sockets, sizeof(int));
			break;

		case FLASH__CREATE_SOCKET:
			sockets[socket_count] =
				create_new_socket(cfg, umem, msgsock);
			send_fd(msgsock, sockets[socket_count]->fd);
			offset += 1;
			socket_count += 1;
			break;

		case FLASH__GET_FR_OFFSET:
			send_data(msgsock, &offset, sizeof(int));
			break;

		case FLASH__CLOSE_CONN:
			for (int i = 0; i < socket_count; i++) {
				xsk_socket__delete(sockets[i]);
			}
			if (xsk_umem__delete(umem->umem)) {
				log_error("UMEM BUSY");
			}
			if (is_primary) {
				if (umem->buffer) {
					munmap(umem->buffer,
					       NUM_FRAMES *
						       cfg->umem->frame_size *
						       total_sockets);
				}
				free(cfg->umem);
				free(cfg->xsk);
				free(cfg);
				free(umem);
			}
			free(sockets);
			goto exit;

		default:
			log_error("Received unknown command: %d\n", cmd);
			exit(EXIT_FAILURE);
			break;
		}
	} while (1);

exit:
	close(msgsock);
	log_info("NF Closed...");
	return NULL;
}

static void int_exit(int sig)
{
	close(unix_socket_server);
	unlink(UNIX_SOCKET_NAME);
}

int main(int argc, char **argv)
{
	printf("MONITOR\n");
	log_set_level_from_env();
	log_info("Starting monitor");

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	pthread_t uds_threads[48];

	monitor__parse_cmdline_args(argc, argv);

	log_info("Starting UDS server");
	unix_socket_server = start_uds_server();
	int msgsock[48], listening = 1, i = 0;

	listen(unix_socket_server, MAX_NUM_OF_CLIENTS);

	while (listening) {
		log_info("Waiting for a connection...");
		msgsock[i] = accept(unix_socket_server, 0, 0);
		log_info("Connection accepted");

		if (msgsock[i] == -1) {
			log_error("Error accepting connection: %s",
				  strerror(errno));
			exit(EXIT_FAILURE);
		}

		if (pthread_create(uds_threads + i, NULL, handle_requests,
				   msgsock + i)) {
			log_error("Error creating UDS thread");
			exit(EXIT_FAILURE);
		}
		i = (i + 1) % 48;
	}

	i = 0;
	if (pthread_join(uds_threads[i], NULL) < 0) {
		log_error("Error joining UDS thread");
		exit(EXIT_FAILURE);
	} else {
		i = (i + 1) % 48;
	}

	close(unix_socket_server);

	return 0;
}
