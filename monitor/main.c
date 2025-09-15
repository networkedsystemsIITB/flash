/* SPDX-License-Identifier: Apache-2.0
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
#include <log.h>

static bool done = false;

static void int_exit(int sig)
{
	(void)sig;
	cleanup_exit();
	sleep(1);
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
		cmd = flash__recv_cmd(msgsock);

		switch (cmd) {
		case FLASH__GET_UMEM:
			flash__recv_data(msgsock, data, sizeof(struct nf_data));
			if (configure_umem(data, &umem) == -1) {
				continue;
			}
			flash__send_fd(msgsock, umem->cfg->umem_fd);
			flash__send_data(msgsock, &umem->nf[data->nf_id]->thread_count, sizeof(int));
			flash__send_data(msgsock, &umem->cfg->umem->size, sizeof(int));
			flash__send_data(msgsock, &umem->cfg->umem_scale, sizeof(int));
			break;

		case FLASH__CREATE_SOCKET:
			if (umem != NULL) {
				struct socket *sock = create_new_socket(umem, data->nf_id);
				flash__send_fd(msgsock, sock->fd);
				flash__send_data(msgsock, &sock->ifqueue, sizeof(int));
			}
			break;

		case FLASH__GET_UMEM_OFFSET:
			int offset = data->nf_id * umem->nf[data->nf_id]->thread_count + umem->nf[data->nf_id]->current_thread_count;
			flash__send_data(msgsock, &offset, sizeof(int));
			break;

		case FLASH__GET_ROUTE_INFO:
			flash__send_data(msgsock, &umem->nf[data->nf_id]->next_size, sizeof(int));
			break;

		case FLASH__GET_BIND_FLAGS:
			flash__send_data(msgsock, &umem->cfg->xsk->bind_flags, sizeof(__u32));
			break;

		case FLASH__GET_XDP_FLAGS:
			flash__send_data(msgsock, &umem->cfg->xsk->xdp_flags, sizeof(__u32));
			break;

		case FLASH__GET_MODE:
			flash__send_data(msgsock, &umem->cfg->xsk->mode, sizeof(__u32));
			break;

		case FLASH__GET_POLL_TIMEOUT:
			flash__send_data(msgsock, &umem->cfg->xsk->poll_timeout, sizeof(int));
			break;

		case FLASH__GET_FRAGS_ENABLED:
			flash__send_data(msgsock, &umem->cfg->frags_enabled, sizeof(bool));
			break;

		case FLASH__GET_IFNAME:
			flash__send_data(msgsock, &umem->cfg->ifname, IF_NAMESIZE);
			break;

		case FLASH__GET_IP_ADDR:
			flash__send_data(msgsock, umem->nf[data->nf_id]->ip, INET_ADDRSTRLEN);
			log_info("NF IP: %s", umem->nf[data->nf_id]->ip);
			break;

		case FLASH__GET_DST_IP_ADDR:
			flash__send_data(msgsock, &umem->nf[data->nf_id]->next_size, sizeof(int));
			log_info("Number of Backends: %d", umem->nf[data->nf_id]->next_size);
			for (int i = 0; i < umem->nf[data->nf_id]->next_size; i++) {
				log_info("Sending IP %s", umem->nf[umem->nf[data->nf_id]->next[i]]->ip);
				log_info("Next NF: %d", umem->nf[data->nf_id]->next[i]);
				flash__send_data(msgsock, umem->nf[umem->nf[data->nf_id]->next[i]]->ip, INET_ADDRSTRLEN);
			}
			break;
		case FLASH__GET_POLLOUT_STATUS:
			flash__send_fd(msgsock, umem->cfg->nf_pollout_status_fd);
			flash__send_data(msgsock, &umem->cfg->nf_pollout_status_size, sizeof(int));
			log_info("SENT POLLOUT STATUS MEM_FD: %d", umem->cfg->nf_pollout_status_fd);
			log_info("SENT POLLOUT STATUS MEM_SIZE: %d", umem->cfg->nf_pollout_status_size);
			break;

		case FLASH__GET_PREV_NF:
			flash__send_data(msgsock, &umem->nf[data->nf_id]->prev_size, sizeof(int));
			log_info("Number of Previous NFs: %d", umem->nf[data->nf_id]->prev_size);
			for (int i = 0; i < umem->nf[data->nf_id]->prev_size; i++) {
				int prev_nf_id = umem->nf[data->nf_id]->prev[i];
				log_info("Sending Previous NF: %d", prev_nf_id);
				flash__send_data(msgsock, &prev_nf_id, sizeof(int));
			}
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
	log_info("Closing NF %d...", data->nf_id);
	free(data);
	return NULL;
}

static void *worker__uds_server(void *arg)
{
	(void)arg;

	unix_socket_server = flash__start_uds_server();
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
			log_error("Error accepting connection: %s", strerror(errno));
			free(msgsock);
			continue;
		}

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
	printf("*****MONITOR*****\n");
	log_set_level_from_env();

	signal(SIGINT, int_exit);
	signal(SIGTERM, int_exit);
	signal(SIGABRT, int_exit);

	pthread_t uds_thread;
	if (pthread_create(&uds_thread, NULL, worker__uds_server, NULL)) {
		log_error("Error creating UDS thread");
		exit(EXIT_FAILURE);
	}

	sleep(1);
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
