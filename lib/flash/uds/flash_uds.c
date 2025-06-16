/* SPDX-License-Identifier: Apache-2.0
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

int flash__recv_cmd(int sockfd)
{
	int cmd, rval;
	rval = read(sockfd, &cmd, sizeof(int));
	if (rval < 0)
		return -1;

	return cmd;
}

int flash__send_cmd(int sockfd, int cmd)
{
	int rval = write(sockfd, &cmd, sizeof(int));
	if (rval < 0)
		return -1;

	return rval;
}

int flash__recv_data(int sockfd, void *data, int size)
{
	int rval = read(sockfd, data, size);
	if (rval < 0)
		return -1;

	return rval;
}

int flash__send_data(int sockfd, void *data, int size)
{
	int rval = write(sockfd, data, size);
	if (rval < 0)
		return -1;

	return rval;
}

int flash__recv_fd(int sockfd, int *_fd)
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

	len = recvmsg(sockfd, &msg, 0);
	if (len < 0) {
		log_error("Recvmsg failed length incorrect.");
		return -1;
	}

	if (buf[0] == 'n') {
		log_error("Received error message -1");
		return -1;
	} else if (buf[0] == 'y') {
		cmsg = CMSG_FIRSTHDR(&msg);
		if (cmsg == NULL) {
			log_error("No ancillary data.");
			return -1;
		}

		*_fd = *(int *)CMSG_DATA(cmsg);

		return 0;
	} else {
		log_error("Received unknown message: %s", buf);
		return -1;
	}
}

int flash__send_fd(int sockfd, int fd)
{
	int ret;
	char cmsgbuf[CMSG_SPACE(sizeof(int))];
	struct msghdr msgh = { 0 };
	struct iovec iov = { 0 };
	char buf[1] = { 'y' };

	if (fd == -1) {
		log_debug("Sending error message -1", fd);
		buf[0] = 'n';

		iov.iov_base = buf;
		iov.iov_len = 1;

		msgh.msg_name = NULL;
		msgh.msg_namelen = 0;
		msgh.msg_iov = &iov;
		msgh.msg_iovlen = 1;

		ret = sendmsg(sockfd, &msgh, 0);

		if (ret == -1) {
			log_error("Sendmsg failed with %s", strerror(errno));
			return -1;
		}

		return ret;
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
	ret = sendmsg(sockfd, &msgh, 0);

	if (ret == -1) {
		log_error("Sendmsg failed with %s", strerror(errno));
		return -1;
	}

	return ret;
}

int flash__start_uds_server(void)
{
	int sockfd;
	int flag = 1;
	struct sockaddr_un server;

	umask(0);

	if (mkdir(UNIX_SOCKET_DIR, 0777) == -1 && errno != EEXIST) {
		log_error("Error creating directory %s: %s", UNIX_SOCKET_DIR, strerror(errno));
		return -1;
	}

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		log_error("Error opening socket stream: %s", strerror(errno));
		return -1;
	}

	unlink(UNIX_SOCKET_NAME);
	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, UNIX_SOCKET_NAME);
	setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(int));

	if (bind(sockfd, (struct sockaddr *)&server, sizeof(struct sockaddr_un))) {
		log_error("Binding to socket stream failed: %s", strerror(errno));
		return -1;
	}

	return sockfd;
}

int flash__start_uds_client(void)
{
	struct sockaddr_un server;
	int sockfd;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0) {
		log_error("Error opening socket stream: %s", strerror(errno));
		return -1;
	}

	server.sun_family = AF_UNIX;
	strcpy(server.sun_path, UNIX_SOCKET_NAME);

	if (connect(sockfd, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) < 0) {
		close(sockfd);
		log_error("Error connecting stream socket: %s", strerror(errno));
		return -1;
	}

	return sockfd;
}
