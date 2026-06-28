/* SPDX-License-Identifier: GPL-2.0 
 *
 * Code taken from https://github.com/mcabranches/mtcp/tree/af_xdp_support and extended by
 * Debojeet Das for Flash NF support using Flash API.
*/

#include "io_module.h"
#ifndef DISABLE_AFXDP

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

/* for mtcp related def'ns */
#include "mtcp.h"
/* for errno */
#include <errno.h>
/* for logging */
#include "debug.h"
/* for num_devices_* */
#include "config.h"
/* for ETHER_CRC_LEN */
#include <net/ethernet.h>

/*----------------------------------------------------------------------------*/
#define MAX_IFNAMELEN (IF_NAMESIZE + 10)

/*
 * Ethernet frame overhead
 */
#define ETHER_IFG 12
#define ETHER_PREAMBLE 8
#define ETHER_OVR (ETHER_CRC_LEN + ETHER_PREAMBLE + ETHER_IFG)

/*----------------------------------------------------------------------------*/

struct afxdp_private_context { // private context on mTCP
	struct config cfg;
	struct nf *nf;
	struct xskvec *recvvecs;
	struct xskvec *sendvecs;
	struct xskvec *dropvecs;
	struct xskvec *allocvecs;
	uint32_t recv_index;
	uint32_t send_index;
	uint32_t drop_index;
	uint32_t alloc_index;
	uint32_t alloc_count;
	struct pollfd fds[1];

	// stats
	uint64_t rx_pkts;
	uint64_t tx_pkts;

	uint64_t total_rx_pkts;
	uint64_t total_tx_pkts;

	uint64_t active_seconds;

	time_t last_print_time;
	time_t window_start;
} __attribute__((aligned(__WORDSIZE)));

/*----------------------------------------------------------------------------*/
static void check_and_print_stats(struct afxdp_private_context *axpc)
{
	time_t now = time(NULL);

	if (now > axpc->last_print_time) {
		if (axpc->rx_pkts > 0 || axpc->tx_pkts > 0) {
			axpc->total_rx_pkts += axpc->rx_pkts;
			axpc->total_tx_pkts += axpc->tx_pkts;
			axpc->active_seconds++;
		}

		axpc->rx_pkts = 0;
		axpc->tx_pkts = 0;
		axpc->last_print_time = now;
	}

	// 10-second window
	if (now - axpc->window_start >= 10) {
		if (axpc->active_seconds > 0) {
			double pps_rx = (double)axpc->total_rx_pkts / axpc->active_seconds;
			double pps_tx = (double)axpc->total_tx_pkts / axpc->active_seconds;

			printf("[AFXDP LAST 10s] Active: %lu s | RX: %.0f pps | TX: %.0f pps\n", axpc->active_seconds, pps_rx, pps_tx);
		}

		// reset window
		axpc->total_rx_pkts = 0;
		axpc->total_tx_pkts = 0;
		axpc->active_seconds = 0;
		axpc->window_start = now;
	}
}

/*----------------------------------------------------------------------------*/
void afxdp_load_module(void);
void afxdp_init_handle(struct mtcp_thread_context *ctxt);
int afxdp_link_devices(struct mtcp_thread_context *ctxt);
int afxdp_recv_pkts(struct mtcp_thread_context *ctxt, int ifidx);
int afxdp_send_pkts(struct mtcp_thread_context *ctxt, int ifidx);
void afxdp_release_pkt(struct mtcp_thread_context *ctxt, int ifidx, unsigned char *pkt_data, int len);
void afxdp_drop_pkts(struct mtcp_thread_context *ctxt);
uint8_t *afxdp_get_wptr(struct mtcp_thread_context *ctxt, int ifidx, uint16_t len);
uint8_t *afxdp_get_rptr(struct mtcp_thread_context *ctxt, int ifidx, int index, uint16_t *len);
int afxdp_select(struct mtcp_thread_context *ctxt);
void afxdp_destroy_handle(struct mtcp_thread_context *ctxt);
int afxdp_dev_ioctl(struct mtcp_thread_context *ctxt, int nif, int cmd, void *argp);

/*----------------------------------------------------------------------------*/
void afxdp_load_module(void)
{
	/* not needed - all initializations done in afxdp_init_handle() */
}

/*----------------------------------------------------------------------------*/
void afxdp_init_handle(struct mtcp_thread_context *ctxt)
{
	struct afxdp_private_context *axpc;
	int j;

	/* create and initialize private I/O module context */
	ctxt->io_private_context = calloc(1, sizeof(struct afxdp_private_context));
	if (ctxt->io_private_context == NULL) {
		TRACE_ERROR("Failed to initialize ctxt->io_private_context: "
			    "Can't allocate memory\n");
		exit(EXIT_FAILURE);
	}

	axpc = (struct afxdp_private_context *)ctxt->io_private_context;

	// m-> come back here to evaluate the multiple interface scenario
	// d-> I am also assuming that there is only one interface
	for (j = 0; j < num_devices_attached; j++) {
		axpc->cfg.app_name = "MTCP";
		axpc->cfg.app_options = NULL;

		// custom argv for the afxdp module
		char *argv[6];
		argv[0] = strdup("mtcp");
		argv[1] = strdup("-u");
		argv[2] = strdup("0");
		argv[3] = strdup("-f");

		char flash_nic_queue_str[8];
		sprintf(flash_nic_queue_str, "%d", ctxt->flash_ctx.flash_nic_queue);
		argv[4] = strdup(flash_nic_queue_str);

		argv[5] = strdup("-t");

		if (flash__parse_cmdline_args(6, argv, &axpc->cfg) < 0)
			goto out_cfg;

		if (flash__configure_nf(&axpc->nf, &axpc->cfg) < 0)
			goto out_cfg;

		log_info("Control Plane setup done...");

		// m-> set the receiving queue to the processing core number
		// d-> i am consdering this info is setup from the config file
		// axpc->cfg.ifname = ifname;
		// axpc->cfg.xsk_if_queue = ctxt->cpu;

		// axpc->packet_buffer_size = NUM_FRAMES * FRAME_SIZE;
		// if (posix_memalign(&axpc->packet_buffer, getpagesize(), /* PAGE_SIZE aligned */
		// 		   axpc->packet_buffer_size)) {
		// 	fprintf(stderr, "ERROR: Can't allocate buffer memory \"%s\"\n", strerror(errno));
		// 	exit(EXIT_FAILURE);
		// }

		// /* Initialize shared packet_buffer for umem usage */
		// axpc->umem = configure_xsk_umem(axpc->packet_buffer, axpc->packet_buffer_size);
		// if (axpc->umem == NULL) {
		// 	fprintf(stderr, "ERROR: Can't create umem \"%s\"\n", strerror(errno));
		// 	exit(EXIT_FAILURE);
		// }

		/* Open and configure the AF_XDP (xsk) socket */
		// axpc->xsk_socket = xsk_configure_socket(&axpc->cfg, axpc->umem);
		// if (axpc->xsk_socket == NULL) {
		// 	fprintf(stderr, "ERROR: Can't setup AF_XDP socket \"%s\"\n", strerror(errno));
		// 	exit(EXIT_FAILURE);
		// }
	}

	memset(axpc->fds, 0, sizeof(axpc->fds));
	axpc->fds[0].fd = axpc->nf->thread[0]->socket->fd; // d-> Assuming single thread for now
	axpc->fds[0].events = POLLIN;

	// d-> initialize send vectors
	axpc->sendvecs = calloc(axpc->cfg.xsk->batch_size, sizeof(struct xskvec));
	if (!axpc->sendvecs) {
		log_error("Failed to allocate xskvecs array");
		goto out_cfg_close;
	}
	axpc->send_index = 0;

	axpc->recvvecs = calloc(axpc->cfg.xsk->batch_size, sizeof(struct xskvec));
	if (!axpc->recvvecs) {
		log_error("Failed to allocate recv xskvecs array");
		free(axpc->sendvecs);
		goto out_cfg_close;
	}
	axpc->recv_index = 0;

	axpc->dropvecs = calloc(axpc->cfg.xsk->batch_size, sizeof(struct xskvec));
	if (!axpc->dropvecs) {
		log_error("Failed to allocate drop xskvecs array");
		free(axpc->sendvecs);
		free(axpc->recvvecs);
		goto out_cfg_close;
	}
	axpc->drop_index = 0;

	axpc->allocvecs = calloc(axpc->cfg.xsk->batch_size, sizeof(struct xskvec));
	if (!axpc->allocvecs) {
		log_error("Failed to allocate alloc xskvecs array");
		free(axpc->sendvecs);
		free(axpc->recvvecs);
		free(axpc->dropvecs);
		goto out_cfg_close;
	}
	axpc->alloc_index = 0;
	axpc->alloc_count = 0;

	// Initialize statistics
	axpc->rx_pkts = 0;
	axpc->tx_pkts = 0;

	axpc->total_rx_pkts = 0;
	axpc->total_tx_pkts = 0;

	axpc->active_seconds = 0;

	axpc->last_print_time = time(NULL);
	axpc->window_start = axpc->last_print_time;

	return;

out_cfg_close:
	flash__xsk_close(&axpc->cfg, axpc->nf);
out_cfg:
	exit(EXIT_FAILURE);
}

/*----------------------------------------------------------------------------*/
int afxdp_link_devices(struct mtcp_thread_context *ctxt)
{
	(void)ctxt; // d-> unused parameter
	/* linking takes place during mtcp_init() */
	return 0;
}

/*----------------------------------------------------------------------------*/
int32_t afxdp_recv_pkts(struct mtcp_thread_context *ctxt, int ifidx)
{
	(void)ifidx; // d-> unused parameter
	int ret, nfds = 1;
	uint32_t nrecv = 0;
	struct afxdp_private_context *axpc;
	axpc = (struct afxdp_private_context *)ctxt->io_private_context;
	struct socket *xsk = axpc->nf->thread[0]->socket; // d-> Assuming single thread for now

	ret = flash__poll(&axpc->cfg, xsk, axpc->fds, nfds);
	if (!(ret == 1 || ret == -2))
		return 0;

	uint32_t to_recv = axpc->cfg.xsk->batch_size;
	nrecv = flash__recvmsg(&axpc->cfg, xsk, axpc->recvvecs, to_recv);

	axpc->rx_pkts += nrecv;

	check_and_print_stats(axpc);
	return nrecv;
}

/*----------------------------------------------------------------------------*/
// m-> function to return the pointers to mTCP (This should iterate through to the number of
// recv pkts and return pointers to pkts to be processed by mTCP)
uint8_t *afxdp_get_rptr(struct mtcp_thread_context *ctxt, int ifidx, int index, uint16_t *len)
{
	(void)ifidx; // d-> unused parameter
	(void)index; // d-> unused parameter
	struct afxdp_private_context *axpc;
	axpc = (struct afxdp_private_context *)ctxt->io_private_context;

	if (axpc->recv_index >= axpc->cfg.xsk->batch_size) {
		return NULL;
	}

	uint8_t *pktbuf = axpc->recvvecs[axpc->recv_index].data;
	*len = axpc->recvvecs[axpc->recv_index].len;

#ifdef MTCP_FLASH_ID_TRAILER
	// flash_ids shaved off
	// [pkt][src_flash_id][dst_flash_id]
	// pkt's destination is me
	// pkt's source is the other flash_id which we need to store in the context
	ctxt->flash_ctx.dst_flash_id = *(uint8_t *)(pktbuf + *len - 2);

	*len -= 2;
#endif /* MTCP_FLASH_ID_TRAILER */

	axpc->recv_index++;

	return pktbuf;
}

/*----------------------------------------------------------------------------*/
// h -> drops everything in recvvecs
// not invoked in mtcp-zero-copy
void afxdp_drop_pkts(struct mtcp_thread_context *ctxt)
{
	struct afxdp_private_context *axpc;
	axpc = (struct afxdp_private_context *)ctxt->io_private_context;

	if (flash__dropmsg(&axpc->cfg, axpc->nf->thread[0]->socket, axpc->recvvecs, axpc->recv_index) != axpc->recv_index) {
		log_error("Failed to drop messages");
		axpc->recv_index = 0;
		return;
	}
	axpc->recv_index = 0;
}

/*----------------------------------------------------------------------------*/
void afxdp_release_pkt(struct mtcp_thread_context *ctxt, int ifidx, unsigned char *pkt_data, int len)
{
	(void)ctxt;	// d-> unused parameter
	(void)ifidx;	// d-> unused parameter
	(void)pkt_data; // d-> unused parameter
	(void)len;	// d-> unused parameter
			/* not needed - drop packets is handled seperately */
}

/*----------------------------------------------------------------------------*/
//m-> for more details of what needs to be done see my notebook (mtcp pktio), pages 44 and 54
//also see the get_wptr dpdk function to see more details
uint8_t *afxdp_get_wptr(struct mtcp_thread_context *ctxt, int nif, uint16_t pktsize)
{
	(void)nif; // d-> unused parameter
	struct afxdp_private_context *axpc;
	axpc = (struct afxdp_private_context *)ctxt->io_private_context;

	if (axpc->send_index >= axpc->cfg.xsk->batch_size) {
		return NULL;
	}

	// batched allocation
	if (axpc->alloc_index >= axpc->alloc_count) {
		uint32_t to_alloc = axpc->cfg.xsk->batch_size;
		axpc->alloc_count = flash__allocmsg(&axpc->cfg, axpc->nf->thread[0]->socket, axpc->allocvecs, to_alloc);
		axpc->alloc_index = 0;
		if (axpc->alloc_count == 0) {
			return NULL;
		}
	}

	struct xskvec *tmpvec = &axpc->allocvecs[axpc->alloc_index++];

	uint8_t *pktbuf = tmpvec->data;
	uint32_t options = 0;

#ifdef MTCP_FLASH_ID_TRAILER
	// reserve 2 bytes at the end of the packet for flash ids (src and dst)
	// [pkt][src_flash_id][dst_flash_id]
	uint8_t src_flash_id = axpc->cfg.nf_id;
	uint8_t dst_flash_id = ctxt->flash_ctx.dst_flash_id;
	ctxt->flash_ctx.dst_flash_id = -1;

	*(uint8_t *)(pktbuf + pktsize) = src_flash_id;
	*(uint8_t *)(pktbuf + pktsize + 1) = dst_flash_id;
	pktsize += 2;
	options = (dst_flash_id << 16) | (options & 0xFFFF);
#endif /* MTCP_FLASH_ID_TRAILER */

	axpc->sendvecs[axpc->send_index].data = pktbuf;
	axpc->sendvecs[axpc->send_index].len = pktsize;
	axpc->sendvecs[axpc->send_index].addr = tmpvec->addr;
	axpc->sendvecs[axpc->send_index++].options = options;

	return pktbuf;
}

/*----------------------------------------------------------------------------*/
int afxdp_send_pkts(struct mtcp_thread_context *ctxt, int nif)
{
	(void)nif; // d-> unused parameter
	struct afxdp_private_context *axpc;
	axpc = (struct afxdp_private_context *)ctxt->io_private_context;

	if (flash__sendmsg(&axpc->cfg, axpc->nf->thread[0]->socket, axpc->sendvecs, axpc->send_index) != axpc->send_index) {
		log_error("Failed to send messages");
		axpc->send_index = 0;
		return -1;
	}
	axpc->tx_pkts += axpc->send_index;
	axpc->send_index = 0;

	check_and_print_stats(axpc);
	return 1;
}

/*----------------------------------------------------------------------------*/
int32_t afxdp_select(struct mtcp_thread_context *ctxt)
{
	(void)ctxt; // d-> unused parameter
	// m-> implement
	// d-> implement
	return 0; // d-> return 0 for now, as select is not implemented
}

/*----------------------------------------------------------------------------*/
void afxdp_destroy_handle(struct mtcp_thread_context *ctxt)
{
	struct afxdp_private_context *axpc;
	axpc = (struct afxdp_private_context *)ctxt->io_private_context;

	free(axpc->recvvecs);
	free(axpc->sendvecs);
	free(axpc->dropvecs);
	flash__xsk_close(&axpc->cfg, axpc->nf);
	free(axpc);
}

/*----------------------------------------------------------------------------*/
io_module_func afxdp_module_func = { .load_module = afxdp_load_module,
				     .init_handle = afxdp_init_handle,
				     .link_devices = afxdp_link_devices,
				     .recv_pkts = afxdp_recv_pkts,
				     .get_rptr = afxdp_get_rptr,
				     .drop_pkts = afxdp_drop_pkts,
				     .release_pkt = afxdp_release_pkt,
				     .get_wptr = afxdp_get_wptr,
				     .send_pkts = afxdp_send_pkts,
				     .select = afxdp_select,
				     .destroy_handle = afxdp_destroy_handle,
				     .dev_ioctl = NULL };
/*----------------------------------------------------------------------------*/
#endif /* !DISABLE_AFXDP */
