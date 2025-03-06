/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <pthread.h>
#include <signal.h>
#include <bpf/libbpf.h>
#include <linux/if_link.h>
#include <stdlib.h>
#include <poll.h>

#include <flash_monitor.h>
#include <flash_uds.h>
#include <flash_params.h>
#include <log.h>
#include <flash_display.h>

static bool done = false;

static void int_exit(int sig)
{
	log_info("Received Signal: %d", sig);
	close(unix_socket_server);
	unlink(UNIX_SOCKET_NAME);
	done = true;
}

static void *handle_nf(void *arg)
{
	int msgsock = *(int *)arg;
	free(arg);
	struct nf_data *data = calloc(1, sizeof(struct nf_data));
	struct umem *umem = NULL;
	int cmd = -1;

	do {
		log_info("Waiting for command...");
		cmd = recv_cmd(msgsock);

		switch (cmd) {
		case FLASH__GET_UMEM:
			recv_data(msgsock, data, sizeof(struct nf_data));
			if (configure_umem(data, &umem) == -1) {
				continue;
			}
			send_fd(msgsock, umem->cfg->umem_fd);
			send_data(msgsock, &umem->nf[data->nf_id]->thread_count,
				  sizeof(int));
			send_data(msgsock, &umem->cfg->umem->size, sizeof(int));
			break;

		case FLASH__CREATE_SOCKET:
			if (umem != NULL) {
				send_fd(msgsock,
					create_new_socket(umem, data->nf_id));
			}
			break;

		case FLASH__GET_UMEM_OFFSET:
			int offset =
				data->nf_id *
					umem->nf[data->nf_id]->thread_count +
				umem->nf[data->nf_id]->current_thread_count;
			send_data(msgsock, &offset, sizeof(int));
			break;

		case FLASH__CLOSE_CONN:
			close_nf(umem, data->umem_id, data->nf_id);
			goto exit;

		default:
			log_error("Received unknown command: %d\n", cmd);
			exit(EXIT_FAILURE);
			break;
		}
	} while (!done);

exit:
	close(msgsock);
	free(data);
	log_info("NF Closed...");
	return NULL;
}

static void *worker__uds_server(void *arg)
{
	(void)arg;

	log_info("Starting UDS server");
	unix_socket_server = start_uds_server();
	struct pollfd fds[1] = {};
	fds[0].fd = unix_socket_server;
	fds[0].events = POLLIN;
	int timeout = 100, ret;
	listen(unix_socket_server, MAX_NUM_OF_CLIENTS);
	log_info("Waiting for NFs to connect...");

	while (!done) {
		ret = poll(fds, 1, timeout);
		if (ret != 1 || !(fds[0].revents & POLLIN))
			continue;

		int *msgsock = malloc(sizeof(int));
		if (!msgsock) {
			log_error("Memory allocation failed");
			continue;
		}

		*msgsock = accept(unix_socket_server, NULL, NULL);
		if (*msgsock == -1) {
			log_error("Error accepting connection: %s",
				  strerror(errno));
			free(msgsock);
			continue;
		}

		log_info("Connection accepted");

		pthread_t thread;
		if (pthread_create(&thread, NULL, handle_nf, msgsock)) {
			log_error("Error creating UDS thread");
			free(msgsock);
			continue;
		}

		pthread_detach(thread);
	}

	close(unix_socket_server);
	return NULL;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	printf("MONITOR\n");
	log_set_level_from_env();
	log_info("Starting monitor");

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	// char *filename = argv[argc - 1];
	// log_info("FILE: %s", filename);
	// log_info("%s", process_input(filename));

	pthread_t uds_thread;
	if (pthread_create(&uds_thread, NULL, worker__uds_server, NULL)) {
		log_error("Error creating UDS thread");
		exit(EXIT_FAILURE);
	}

	pthread_t prompt_thread;
	if (pthread_create(&prompt_thread, NULL, init_prompt, NULL)) {
		log_error("Error creating UDS thread");
		exit(EXIT_FAILURE);
	}

	if (pthread_join(uds_thread, NULL) < 0) {
		log_error("Error joining UDS thread");
		exit(EXIT_FAILURE);
	}

	if (pthread_cancel(prompt_thread) < 0) {
		log_error("Error canceling prompt thread");
		exit(EXIT_FAILURE);
	}

	close(unix_socket_server);

	return 0;
}
