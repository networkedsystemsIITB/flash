/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sched.h>
#include <stdlib.h>
#include <unistd.h>

#include <flash_uds.h>
#include <log.h>

#include "flash_monitor.h"

int unix_socket_server;

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

static struct xsk_umem_info *__configure_umem(void *buffer, uint64_t size,
					      struct config *cfg)
{
	struct xsk_umem_info *umem;
	struct xsk_umem_config umem_cfg = {
		/**
         * It is recommended that the fill ring size >= HW RX ring size +
         * AF_XDP RX ring size. Make sure you fill up the fill ring
         * with buffers at regular intervals, and you will with this setting
         * avoid allocation failures in the driver. These are usually quite
         * expensive since drivers have not been written to assume that
         * allocation failures are common. For regular sockets, kernel
         * allocated memory is used that only runs out in OOM situations
         * that should be rare.
         */
		.fill_size =
			(size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)2,
		.comp_size = (size_t)XSK_RING_CONS__DEFAULT_NUM_DESCS,
		.frame_size = cfg->umem->frame_size,
		.frame_headroom = XSK_UMEM__DEFAULT_FRAME_HEADROOM,
		.flags = cfg->umem->flags
	};

	log_info("Creating UMEM info");
	umem = calloc(1, sizeof(*umem));
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
	ret = xsk_umem__create(&umem->umem, buffer, size, &umem->fq, &umem->cq,
			       &umem_cfg);

	if (ret) {
		log_error("errno: %d/\"%s\"\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	umem->buffer = buffer;
	return umem;
}

struct xsk_umem_info *flash__setup_umem(struct config *cfg)
{
	struct rlimit rlim = { RLIM_INFINITY, RLIM_INFINITY };
	struct xsk_umem_info *umem;
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

	size = (size_t)NUM_FRAMES * (size_t)cfg->umem->frame_size *
	       (size_t)cfg->total_sockets;

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

	// /* Create the sockets.. */
	log_info("Creating UMEM info");
	umem = __configure_umem(packet_buffer, size, cfg);
	cfg->umemfd = fd;

	return umem;
}

/**
 * @brief Configure the AF_XDP Sockets and its rings. In case of shared UMEM the socket
 * is created with the shared UMEM. The function also updates the xsk_map with the
 * socket fd.
 *
 * @param cfg
 * @param umem
 * @return struct monitor_xsk_socket_info*
 */
struct monitor_xsk_socket_info *flash__setup_xsk(struct config *cfg,
						 struct xsk_umem_info *umem)
{
	struct xsk_socket_config xsk_cfg;
	struct monitor_xsk_socket_info *xsk_info;
	int ret;
	int sock_opt;

	xsk_info = calloc(1, sizeof(*xsk_info));
	if (!xsk_info) {
		log_error("Memory allocation failed, errno: %d/\"%s\"\n", errno,
			  strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD macro ensures that the default program is not loaded by the library. */
	xsk_info->umem = *umem;
	xsk_cfg.rx_size = (size_t)XSK_RING_CONS__DEFAULT_NUM_DESCS;
	xsk_cfg.tx_size = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS;
	xsk_cfg.libbpf_flags = ((cfg->custom_xsk) || (cfg->reduce_cap)) ?
				       XSK_LIBBPF_FLAGS__INHIBIT_PROG_LOAD :
				       0;
	xsk_cfg.xdp_flags = cfg->xsk->xdp_flags;
	xsk_cfg.bind_flags = cfg->xsk->bind_flags;

	/**
     * The library call does the following steps of the AF_XDP control path:
     * 1. If UMEM is shared with another socket it creates a new socket.
     * 2. If required it creates new FQ and CQ for the socket.
     * 	  (On unique ifindex + queue + netnscookie)
     * 3. It creates new TX/RX rings if required. (Debo: When?)
     * 4. Binds the socket to the given interface and queue.
     * 5. Loads the default XDP program (if required).
     */

	if (cfg->is_primary)
		log_info("IS PRIMARY!!!");

	if (cfg->thread_count == 0) {
		log_info("QUEUEUEUEUEUEUE1 ID: %d %d %d", cfg->xsk->ifqueue,
			 cfg->thread_count, cfg->is_primary);
		ret = xsk_socket__create(&xsk_info->xsk, cfg->ifname,
					 cfg->xsk->ifqueue, umem->umem,
					 &xsk_info->mxsk.rx, &xsk_info->mxsk.tx,
					 &xsk_cfg);
		cfg->thread_count++;
	} else {
		log_info("QUEUEUEUEUEUEUE2 ID: %d %d %d", cfg->xsk->ifqueue,
			 cfg->thread_count, cfg->is_primary);
		ret = xsk_socket__create_shared(
			&xsk_info->xsk, cfg->ifname, cfg->xsk->ifqueue,
			umem->umem, &xsk_info->mxsk.rx, &xsk_info->mxsk.tx,
			&xsk_info->mxsk.fill, &xsk_info->mxsk.comp, &xsk_cfg);
		cfg->thread_count++;
	}
	if (ret) {
		log_error("xsk_socket__create failed, errno: %d/\"%s\"\n",
			  errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	log_info("SOCKET FD: %d", xsk_info->xsk->fd);

	/* Enable and configure busy poll */
	if (cfg->xsk->mode__busy_poll && !(cfg->xsk->bind_flags & XDP_COPY)) {
		sock_opt = 1;
		if (setsockopt(xsk_info->xsk->fd, SOL_SOCKET,
			       SO_PREFER_BUSY_POLL, (void *)&sock_opt,
			       sizeof(sock_opt)) < 0) {
			log_error("setsockopt 1 failed, errno: %d/\"%s\"\n",
				  errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		sock_opt = 20;
		if (setsockopt(xsk_info->xsk->fd, SOL_SOCKET, SO_BUSY_POLL,
			       (void *)&sock_opt, sizeof(sock_opt)) < 0) {
			log_error("setsockopt 2 failed, errno: %d/\"%s\"\n",
				  errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		sock_opt = 64; // poll budget
		if (setsockopt(xsk_info->xsk->fd, SOL_SOCKET,
			       SO_BUSY_POLL_BUDGET, (void *)&sock_opt,
			       sizeof(sock_opt)) < 0) {
			log_error("setsockopt 3 failed, errno: %d/\"%s\"\n",
				  errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}

	return xsk_info;
}

void init_config(struct config *cfg)
{
	cfg->xsk = calloc(1, sizeof(struct xsk_config));
	cfg->umem = calloc(1, sizeof(struct xsk_umem_config));
	if (!cfg->xsk || !cfg->umem) {
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	/* Default config values */
	cfg->reduce_cap = false;
	cfg->custom_xsk = false;
	cfg->xsk->ifqueue = -1;
	cfg->xsk->bind_flags = XDP_USE_NEED_WAKEUP;
	cfg->xsk->batch_size = BATCH_SIZE;
	cfg->xsk->mode__need_wakeup = false;
	cfg->xsk->mode__zero_copy = false;
	cfg->xsk->mode__poll = false;
	cfg->xsk->mode__busy_poll = false;
	cfg->umem->frame_size = FRAME_SIZE;
	cfg->thread_count = 0;
	cfg->umemfd = -1;
}

int create_new_umem(struct config **_cfg, struct xsk_umem_info **_umem,
		    int total_sockets)
{
	struct config *cfg = calloc(1, sizeof(struct config));
	if (!cfg) {
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}
	init_config(cfg);
	cfg->xsk->mode__busy_poll = true;
	cfg->total_sockets = total_sockets;
	printf("TOTAL SOCKETS: %d\n", cfg->total_sockets);
	cfg->xsk->mode__zero_copy = true;
	cfg->xsk->bind_flags &= ~XDP_COPY;
	cfg->xsk->bind_flags |= XDP_ZEROCOPY;

	struct xsk_umem_info *umem = flash__setup_umem(cfg);
	*_umem = umem;
	*_cfg = cfg;
	log_info("Primary UMEM Setup DONE");

	return cfg->umemfd;
}

struct xsk_socket *create_new_socket(struct config *cfg,
				     struct xsk_umem_info *umem, int msgsock)
{
	char ifname[IF_NAMESIZE];
	recv_data(msgsock, ifname, IF_NAMESIZE);
	for (int i = 0; i < IF_NAMESIZE; i++)
		cfg->ifname[i] = ifname[i];

	recv_data(msgsock, &cfg->xsk->ifqueue, sizeof(int));

	log_info("IFNAME: %s %s", cfg->ifname, ifname);
	struct monitor_xsk_socket_info *xsk = flash__setup_xsk(cfg, umem);
	return xsk->xsk;
}
