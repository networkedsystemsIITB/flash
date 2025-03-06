/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <sys/un.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <log.h>

#include "flash_uds.h"

void send_cmd(int sockfd, int cmd)
{
	int rval = write(sockfd, &cmd, sizeof(int));
	if (rval < 0) {
		log_error("Error writing stream cmd");
		exit(EXIT_FAILURE);
	}
}

void send_data(int sockfd, void *data, int size)
{
	int rval = write(sockfd, data, size);
	if (rval < 0) {
		log_error("Error writing stream data");
		exit(EXIT_FAILURE);
	}
}

void recv_data(int sockfd, void *data, int size)
{
	log_info("SOCKET: %d", sockfd);
	int rval = read(sockfd, data, size);
	if (rval < 0) {
		log_error("Error reading stream data");
		exit(EXIT_FAILURE);
	}
}

int recv_cmd(int sockfd)
{
	int cmd, rval;
	rval = read(sockfd, &cmd, sizeof(int));
	log_info("Received command: %d", cmd);
	if (rval < 0) {
		log_error("Error reading stream cmd");
		exit(EXIT_FAILURE);
	}

	return cmd;
}

int send_fd(int sockfd, int fd)
{
	char cmsgbuf[CMSG_SPACE(sizeof(int))];
	struct msghdr msgh;
	struct iovec iov;
	char buf[1];

	if (fd == -1) {
		log_error("Incorrect fd = %d\n", fd);
		exit(EXIT_FAILURE);
	}

	/* We must transmit at least 1 byte of real data in order
     * to send some other ancillary data */
	iov.iov_base = buf;
	iov.iov_len = 1;

	msgh.msg_name = NULL;
	msgh.msg_namelen = 0;
	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;
	msgh.msg_flags = 0;
	msgh.msg_control = cmsgbuf;
	msgh.msg_controllen = CMSG_LEN(sizeof(int));

	struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msgh);

	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));

	/* Write the fd as ancillary data */
	*(int *)CMSG_DATA(cmsg) = fd;
	int ret = sendmsg(sockfd, &msgh, 0);

	if (ret == -1) {
		log_error("Sendmsg failed with %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	return ret;
}

int start_uds_server(void)
{
	int sockfd;
	int flag = 1;
	struct sockaddr_un server;

	umask(0);

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		log_error("Error opening socket stream: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	log_info("SOCK CREATED::::");

	unlink(UNIX_SOCKET_NAME);

	log_info("UNLINKED ::::");

	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, UNIX_SOCKET_NAME);

	log_info("COPIED TO SUN_PATH::::");

	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

	log_info("SET SOCK OPT::::");

	if (bind(sockfd, (struct sockaddr *)&server,
		 sizeof(struct sockaddr_un))) {
		log_error("Binding to socket stream failed: %s",
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	log_info("BINDED::::");

	return sockfd;
}

int start_uds_client(void)
{
	struct sockaddr_un server;
	int sockfd;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		log_error("Error opening socket stream: %s", strerror(errno));
		exit(EXIT_FAILURE);
	}

	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, UNIX_SOCKET_NAME);

	if (connect(sockfd, (struct sockaddr *)&server,
		    sizeof(struct sockaddr_un)) < 0) {
		close(sockfd);
		log_error("Error connecting stream socket: %s",
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	return sockfd;
}

int recv_fd(int sockfd, int *_fd)
{
	char cms[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *cmsg;
	struct msghdr msg;
	struct iovec iov;
	char buf[1];
	int len;

	iov.iov_base = buf;
	iov.iov_len = 1;

	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;
	msg.msg_control = (caddr_t)cms;
	msg.msg_controllen = sizeof(cms);

	log_info("Receiving MSG");
	len = recvmsg(sockfd, &msg, 0);

	if (len < 0) {
		log_error("Recvmsg failed length incorrect.\n");
		exit(EXIT_FAILURE);
	}

	log_info("Received MSG");

	cmsg = CMSG_FIRSTHDR(&msg);
	*_fd = *(int *)CMSG_DATA(cmsg);

	return 0;
}
