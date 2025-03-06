/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <fcntl.h>
#include <sys/resource.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

#include <flash_uds.h>
#include <log.h>
#include <flash_cfgparser.h>
#include <flash_common.h>

#include "flash_monitor.h"

static struct NFGroup *nfg;
int unix_socket_server;

void close_nf(struct umem *umem, int umem_id, int nf_id)
{
	struct xdp_mmap_offsets off;
	int err;
	if (umem->current_nf_count == 0)
		return;
	for (int i = 0; i < umem->nf[nf_id]->thread_count; i++) {
		if (umem->nf[nf_id]->current_thread_count == 0)
			continue;
		struct socket *socket = umem->nf[nf_id]->thread[i]->socket;
		if (socket == NULL)
			continue;
		xsk_socket__delete(umem->nf[nf_id]->thread[i]->xsk);
		err = xsk_get_mmap_offsets(socket->fd, &off);
		if (!err) {
			munmap(socket->fill.ring - off.fr.desc,
			       off.fr.desc + umem->cfg->umem_config->fill_size *
						     sizeof(__u64));
			munmap(socket->comp.ring - off.cr.desc,
			       off.cr.desc + umem->cfg->umem_config->comp_size *
						     sizeof(__u64));
		}
		free(socket);
	}
	umem->cfg->current_socket_count -= umem->nf[nf_id]->thread_count;
	if (umem->cfg->current_socket_count < 0)
		umem->cfg->current_socket_count = 0;
	umem->nf[nf_id]->current_thread_count = 0;
	umem->current_nf_count--;
	if (umem->current_nf_count < 0)
		umem->current_nf_count = 0;

	if (umem->umem_info && umem->umem_info->umem) {
		if (xsk_umem__delete(umem->umem_info->umem) < 0) {
			log_info("UMEM refcount == %d, not deleting UMEM",
				 umem->umem_info->umem->refcount);
		} else {
			log_info("UMEM refcount == 0, deleting UMEM");
			close(umem->cfg->umem_fd);
			if (umem->cfg->umem->buffer) {
				munmap(umem->cfg->umem->buffer,
				       umem->cfg->umem->size);
				umem->cfg->umem->buffer = NULL;
				umem->cfg->umem->size = 0;
			}
			umem->cfg->umem_fd = -1;
			free(umem->cfg->umem_config);
			free(umem->cfg->xsk_config);
			free(umem->umem_info);
		}
	} else {
		log_info("UMEM for nf %d having umem_id %d does not exist",
			 nf_id, umem_id);
	}
}

static void close_nfg(void)
{
	if (nfg == NULL)
		return;
	for (int i = 0; i < nfg->umem_count; i++) {
		for (int j = 0; j < nfg->umem[i]->nf_count; j++) {
			close_nf(nfg->umem[i], nfg->umem[i]->id,
				 nfg->umem[i]->nf[i]->id);
		}
	}
}

const char *process_input(char *input)
{
	if (strncmp(input, "load", 4) == 0) {
		nfg = parse_json(input + 5);
		return input;
	} else if (strncmp(input, "unload", 6) == 0) {
		close_nfg();
		nfg = NULL;
		free_nf_group(nfg);
		return input;
	} else {
		return "Invalid command";
	}
	return NULL;
}

static int create_umem_fd(size_t size)
{
	int fd, ret;

	fd = memfd_create("UMEM0", MFD_ALLOW_SEALING);
	if (fd == -1)
		exit(1);

	ret = ftruncate(fd, size);
	if (ret == -1)
		exit(1);

	ret = fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK);
	if (ret == -1)
		exit(1);

	ret = fcntl(fd, F_ADD_SEALS, F_SEAL_SEAL);
	if (ret == -1)
		exit(1);

	return fd;
}

static void __configure_umem(struct umem *umem)
{
	log_info("Creating UMEM info");
	umem->umem_info = calloc(1, sizeof(struct xsk_umem_info));
	if (!umem) {
		log_error("ERROR: calloc failed \"%s\"\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	/**
     * This library call does the following steps of the AF_XDP control path:
     * 1. Socket creation with socket() syscall
     * 2. setsockopt for UMEM registration.
     * 3. Creation of FQ and CQ with setsockopt(), getsockopt() and mmap()
     *
     * If the user doesn't want to do the socket() syscall for any privilege
     * issues it can use xsk_umem__create_with_fd() library call where the fd
     * is passed. The fd can be procured from other privileged program which
     * has opened the AF_XDP socket.
     */
	int ret = 0;
	log_info("Creating UMEM");
	ret = xsk_umem__create(&umem->umem_info->umem, umem->cfg->umem->buffer,
			       umem->cfg->umem->size, &umem->umem_info->fq,
			       &umem->umem_info->cq, umem->cfg->umem_config);

	if (ret) {
		log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	return;
}

static void flash__setup_umem(struct umem *umem)
{
	struct rlimit rlim = { RLIM_INFINITY, RLIM_INFINITY };
	void *packet_buffer;
	struct sched_param schparam;
	int ret, fd, flags;
	size_t size;

	if (setrlimit(RLIMIT_MEMLOCK, &rlim)) {
		log_error(
			"ERROR: (UMEM setup) setrlimit(RLIMIT_MEMLOCK) \"%s\"\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	log_info("Setting priority to 0");
	memset(&schparam, 0, sizeof(schparam));
	schparam.sched_priority = 0;
	ret = sched_setscheduler(0, 0, &schparam);
	if (ret) {
		log_error("Error(%d) in setting priority(%d): %s\n", errno, 0,
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	log_info("TOTAL SOCKET IN JSON: %d", umem->cfg->total_sockets);

	size = (size_t)NUM_FRAMES * (size_t)umem->cfg->umem->frame_size *
	       (size_t)umem->cfg->total_sockets;

	log_info("UMEM size: %lu", size);

	log_info("Creating UMEM");
	fd = create_umem_fd(size);
	flags = MAP_SHARED;

	/* Reserve memory for the umem. Use hugepages if unaligned chunk mode is enabled */
	log_info("Reserving memory for UMEM");
	packet_buffer = mmap(NULL, size, PROT_READ | PROT_WRITE, flags, fd, 0);
	if (packet_buffer == MAP_FAILED) {
		log_error("ERROR: (UMEM setup) mmap failed \"%s\"\n",
			  strerror(errno));
		exit(EXIT_FAILURE);
	}
	umem->cfg->umem->buffer = packet_buffer;
	umem->cfg->umem->size = size;
	// /* Create the sockets.. */
	log_info("Creating UMEM info");
	__configure_umem(umem);
	umem->cfg->umem_fd = fd;

	return;
}

/**
 * @brief Configure the AF_XDP Sockets and its rings. In case of shared UMEM the socket
 * is created with the shared UMEM. The function also updates the xsk_map with the
 * socket fd.
 *
 * @param cfg
 * @param umem
 * @return struct xsk_socket_info*
 */
static int flash__setup_xsk(struct umem *umem, int nf_id)
{
	int ret;
	int sock_opt;
	int umem_ref_count = umem->cfg->current_socket_count;
	int nf_thread_count = umem->nf[nf_id]->current_thread_count;
	int ifqueue = umem->cfg->ifqueue[nf_id * umem->nf[nf_id]->thread_count +
					 nf_thread_count];
	char *ifname = umem->cfg->ifname;

	struct xsk_socket *xsk;
	struct xsk_umem *_umem = umem->umem_info->umem;
	struct xsk_socket_config *xsk_config = umem->cfg->xsk_config;

	struct socket *socket = calloc(1, sizeof(struct socket));
	if (!socket) {
		log_error("Memory allocation failed, errno: %d/\"%s\"\n", errno,
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	/**
     * The library call does the following steps of the AF_XDP control path:
     * 1. If UMEM is shared with another socket it creates a new socket.
     * 2. If required it creates new FQ and CQ for the socket.
     * 	  (On unique ifindex + queue + netnscookie)
     * 3. It creates new TX/RX rings if required. (Debo: When?)
     * 4. Binds the socket to the given interface and queue.
     * 5. Loads the default XDP program (if required).
     */
	if (umem_ref_count == 0) {
		log_info("Creating socket #%d for a UMEM with queue #%d",
			 umem_ref_count, ifqueue);
		ret = xsk_socket__create(&xsk, ifname, ifqueue, _umem,
					 &socket->rx, &socket->tx, xsk_config);
	} else {
		log_info("Creating socket #%d for a UMEM with queue #%d",
			 umem_ref_count, ifqueue);
		ret = xsk_socket__create_shared(&xsk, ifname, ifqueue, _umem,
						&socket->rx, &socket->tx,
						&socket->fill, &socket->comp,
						xsk_config);
	}
	if (ret) {
		log_error(
			"xsk_socket__create failed(check available queues), errno: %d/\"%s\"\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	socket->fd = xsk_socket__fd(xsk);
	log_info("SOCKET FD: %d", socket->fd);
	umem->nf[nf_id]->thread[nf_thread_count]->xsk = xsk;
	umem->nf[nf_id]->thread[nf_thread_count]->socket = socket;
	umem->nf[nf_id]->current_thread_count++;
	umem->cfg->current_socket_count++;

	/* Enable and configure busy poll */
	if (umem->cfg->xsk->mode__busy_poll &&
	    !(umem->cfg->xsk->bind_flags & XDP_COPY)) {
		sock_opt = 1;
		if (setsockopt(socket->fd, SOL_SOCKET, SO_PREFER_BUSY_POLL,
			       (void *)&sock_opt, sizeof(sock_opt)) < 0) {
			log_error("setsockopt 1 failed, errno: %d/\"%s\"\n",
				  errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		sock_opt = 20;
		if (setsockopt(socket->fd, SOL_SOCKET, SO_BUSY_POLL,
			       (void *)&sock_opt, sizeof(sock_opt)) < 0) {
			log_error("setsockopt 2 failed, errno: %d/\"%s\"\n",
				  errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		sock_opt = 64; // poll budget
		if (setsockopt(socket->fd, SOL_SOCKET, SO_BUSY_POLL_BUDGET,
			       (void *)&sock_opt, sizeof(sock_opt)) < 0) {
			log_error("setsockopt 3 failed, errno: %d/\"%s\"\n",
				  errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
		log_info("SET BUSY POLL OPS");
	}

	return socket->fd;
}

static void init_config(struct config *cfg)
{
	log_info("CONFIG_INIT");
	cfg->umem->frame_size = FRAME_SIZE;
	cfg->xsk->batch_size = BATCH_SIZE;
	cfg->xsk->mode__zero_copy = true;
	cfg->xsk->bind_flags &= ~XDP_COPY;
	cfg->xsk->bind_flags |= XDP_ZEROCOPY;
	cfg->umem_fd = -1;
}

int configure_umem(struct nf_data *data, struct umem **_umem)
{
	if (nfg == NULL) {
		log_error("First load config file");
		return -1;
	}

	struct umem *umem = nfg->umem[data->umem_id];
	umem->current_nf_count++;
	if (umem->cfg->umem_fd != 0 && umem->cfg->umem_fd != -1) {
		*_umem = umem;
		return umem->cfg->umem_fd;
	}

	init_config(umem->cfg);
	setup_xsk_config(&umem->cfg->xsk_config, &umem->cfg->umem_config,
			 umem->cfg);
	flash__setup_umem(umem);

	*_umem = umem;
	return umem->cfg->umem_fd;
}

int create_new_socket(struct umem *umem, int nf_id)
{
	return flash__setup_xsk(umem, nf_id);
}
