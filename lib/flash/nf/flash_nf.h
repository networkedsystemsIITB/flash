/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_NF_H
#define __FLASH_NF_H

#include <flash_defines.h>
#include <poll.h>

#define XDP_PKT_CONTD (1 << 0)
#define IS_EOP_DESC(options) (!((options) & XDP_PKT_CONTD))

struct xskvec {
	void *data;	  /* Pointer to data. */
	uint32_t len;	  /* Length of data. */
	uint64_t addr;	  /* Original address */
	uint32_t options; /* Optional flags */
};

struct xskmsghdr {
	struct xskvec *msg_iov; /* Vector of data to send/receive into. */
	uint32_t msg_len;	/* Number of vectors */
};

struct stats_conf {
	struct nf *nf;
	struct config *cfg;
};

struct ether_addr;

/* Control Path APIs */

/** 
 * Configure the NF with the provided configuration.
 * Communicates with the monitor to set up the NF.
 * 
 * @param nf: Pointer to the nf structure pointer to be configured.
 * @param cfg: Pointer to the configuration structure.
 *
 * This function sets up the NF with the provided configuration, including
 * memory mapping, socket setup, and thread initialization.
 * Therefore nf is a pointer to a pointer, where as cfg is a pointer to the configuration structure.
 *
 * @return 0 on success, or -1 on failure.
 */
int flash__configure_nf(struct nf **_nf, struct config *cfg);

/**
 * Wait for a signal from the server to indicate that server wants to close the nf.
 * This function sets the UDS socket to non-blocking mode and checks
 * for incoming signals until it receives one or the connection is closed.
 *
 * @param cfg Pointer to the configuration structure.
 */
void flash__wait(struct config *cfg);

/**
 * Close the NF and clean up resources.
 * This function unmaps memory regions, frees allocated structures,
 * and closes file descriptors associated with the NF.
 *
 * @param cfg: Pointer to the configuration structure.
 * @param nf: Pointer to the nf structure to be closed.
 */
void flash__xsk_close(struct config *cfg, struct nf *nf);

/* Data Path APIs */

/**
 * Poll the NF for incoming packets.
 * 
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 * @param fds: Pointer to the array of pollfd structures.
 * @param nfds: Number of file descriptors to poll.
 * 
 * @return Number of file descriptors that are ready for reading, or -2 if polling is not enabled.
 * 0 if timeout occurs or no file descriptors are ready. -1 on error.
 */
int flash__poll(struct config *cfg, struct socket *xsk, struct pollfd *fds, nfds_t nfds);

/**
 * Receive messages from the socket.
 * 
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 * @param xskvecs: Pointer to the array of xskvec structures to receive data into.
 * @param nrecv: Number of messages to receive.
 * 
 * @return Number of messages received, or 0 if no messages are available.
 */
size_t flash__recvmsg(struct config *cfg, struct socket *xsk, struct xskvec *xskvecs, uint32_t nrecv);

/**
 * Send messages through the socket.
 * 
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 * @param xskvecs: Pointer to the array of xskvec structures containing data to send.
 * @param nsend: Number of messages to send.
 * 
 * @return Number of messages sent, or 0 if no messages were sent.
 */
size_t flash__sendmsg(struct config *cfg, struct socket *xsk, struct xskvec *xskvecs, uint32_t nsend);

/**
 * Use this function in when there are multiple next NFs with different/variable throughput.
 * It tracks the outstanding tx per NF and drops packets if necessary.
 *
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 * @param xskvecs: Pointer to the array of xskvec structures containing received data.
 * @param nrecv: Number of messages received.
 * @param sendvecs: Pointer to the array of xskvec structures to send data
 * @param nsend: Pointer to the number of messages to send.
 * @param dropvecs: Pointer to the array of xskvec structures to drop data
 * @param ndrop: Pointer to the number of messages to drop.
 *
 * @return void
 */
void flash__track_tx_and_drop(struct config *cfg, struct socket *xsk, struct xskvec *xskvecs, uint32_t nrecv, struct xskvec *sendvecs,
			      uint32_t *nsend, struct xskvec *dropvecs, uint32_t *ndrop);

/**
 * Drop messages from the socket.
 * 
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 * @param xskvecs: Pointer to the array of xskvec structures containing data to drop.
 * @param ndrop: Number of messages to drop.
 * 
 * @return Number of messages dropped, or 0 if no messages were dropped.
 */
size_t flash__dropmsg(struct config *cfg, struct socket *xsk, struct xskvec *xskvecs, uint32_t ndrop);

/**
 * Allocate memory for messages to be sent.
 * 
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 * @param xskvecs: Pointer to the array of xskvec structures to allocate memory for.
 * @param nalloc: Number of messages to allocate memory for.
 * 
 * @return Number of messages allocated, or 0 on failure.
 */
size_t flash__allocmsg(struct config *cfg, struct socket *xsk, struct xskvec *xskvecs, uint32_t nalloc);

int flash__oldpoll(struct socket *xsk, struct pollfd *fds, nfds_t nfds, int timeout);
size_t flash__oldrecvmsg(struct config *cfg, struct socket *xsk, struct xskmsghdr *msg);
size_t flash__oldsendmsg(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t nsend);
size_t flash__olddropmsg(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t ndrop);

/* Helper APIs */

/**
 * Thread function to periodically dump statistics of the NF.
 * 
 * @param conf: Pointer to the stats_conf structure containing NF and config.
 * 
 * This routine should be invoked via threads, and it will periodically clear the terminal
 * and dump statistics for each socket in the NF.
 */
void *flash__stats_thread(void *conf);

/**
 * Get the MAC address of the interface specified in the configuration.
 * @param cfg: Pointer to the configuration structure.
 * @param addr: Pointer to the ether_addr structure to store the MAC address.
 * 
 * @return 0 on success, or -1 on failure.
 */
int flash__get_macaddr(struct config *cfg, struct ether_addr *addr);

/**
 * Dump the contents of a packet in hexadecimal format.
 * 
 * @param pkt: Pointer to the packet data.
 * @param length: Length of the packet data.
 * @param verbose: If true, print additional information.
 * 
 * This function prints the packet data in a human-readable hexadecimal format.
 */
void flash__hex_dump(void *pkt, size_t length, bool verbose);

/* Advanced APIs */

void flash__populate_fill_ring(struct thread **thread, int frame_size, int total_sockets, int umem_offset, int umem_scale);

/**
 * Get the current time in nanoseconds.
 * @param cfg: Pointer to the configuration structure.
 *
 * Returns the current time in nanoseconds since the epoch.
 */
unsigned long flash__get_nsecs(struct config *cfg);

/**
 * Dump statistics for the given socket.
 * @param cfg: Pointer to the configuration structure.
 * @param xsk: Pointer to the socket structure.
 */
void flash__dump_stats(struct config *cfg, struct socket *xsk);

/* Experimental */
size_t flash__sendmsg_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, struct xskvec **msgiov, uint32_t nsend);
size_t flash__recvmsg_us(struct config *cfg, struct socket *xsk, struct socket *xsk_first, struct xskmsghdr *msg);
size_t flash__dropmsg_us(struct config *cfg, struct socket *xsk, struct xskvec **msgiov, uint32_t ndrop);
#endif /* __FLASH_NF_H */
