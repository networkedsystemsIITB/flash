/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <log.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>

#include "flash_nf.h"

char spinner[] = { '/', '-', '\\', '|' };
static int spinner_index = 0;

static int get_irqs(struct config *cfg)
{
	char count_path[PATH_MAX];
	int total_intrs = -1;
	FILE *f_count_proc;
	char line[4096];

	snprintf(count_path, sizeof(count_path), "/sys/kernel/irq/%i/per_cpu_count", cfg->irq_no);
	f_count_proc = fopen(count_path, "r");
	if (f_count_proc == NULL) {
		log_error("Failed to open %s\n", count_path);
		return total_intrs;
	}

	if (fgets(line, sizeof(line), f_count_proc) == NULL || line[strlen(line) - 1] != '\n') {
		log_error("Error reading from %s\n", count_path);
	} else {
		static const char com[2] = ",";
		char *token;

		total_intrs = 0;
		token = strtok(line, com);
		while (token != NULL) {
			/* sum up interrupts across all cores */
			total_intrs += atoi(token);
			token = strtok(NULL, com);
		}
	}

	fclose(f_count_proc);

	return total_intrs;
}

size_t flash__get_nsecs(struct config *cfg)
{
	struct timespec ts;

	clock_gettime(cfg->clock, &ts);
	return ts.tv_sec * 1000000000UL + ts.tv_nsec;
}

static int __xsk_get_xdp_stats(int fd, struct socket *xsk)
{
	struct xdp_statistics stats;
	socklen_t optlen;
	int err;

	optlen = sizeof(stats);
	err = getsockopt(fd, SOL_XDP, XDP_STATISTICS, &stats, &optlen);
	if (err)
		return err;

	if (optlen == sizeof(struct xdp_statistics)) {
		xsk->ring_stats.rx_dropped_npkts = stats.rx_dropped;
		xsk->ring_stats.rx_invalid_npkts = stats.rx_invalid_descs;
		xsk->ring_stats.tx_invalid_npkts = stats.tx_invalid_descs;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
		xsk->ring_stats.rx_full_npkts = stats.rx_ring_full;
		xsk->ring_stats.rx_fill_empty_npkts = stats.rx_fill_ring_empty_descs;
		xsk->ring_stats.tx_empty_npkts = stats.tx_ring_empty_descs;
#endif
		return 0;
	}

	return -EINVAL;
}

static void __dump_app_stats(struct socket *xsk, long diff)
{
	double rx_empty_polls_ps, fill_fail_polls_ps, copy_tx_sendtos_ps, tx_wakeup_sendtos_ps, opt_polls_ps, backpressure_sleep_ps;

	rx_empty_polls_ps = (xsk->app_stats.rx_empty_polls - xsk->app_stats_prev.rx_empty_polls) * 1000000000. / diff;
	fill_fail_polls_ps = (xsk->app_stats.fill_fail_polls - xsk->app_stats_prev.fill_fail_polls) * 1000000000. / diff;
	copy_tx_sendtos_ps = (xsk->app_stats.copy_tx_sendtos - xsk->app_stats_prev.copy_tx_sendtos) * 1000000000. / diff;
	backpressure_sleep_ps = (xsk->app_stats.backpressure - xsk->app_stats_prev.backpressure) * 1000000000. / diff;
	tx_wakeup_sendtos_ps = (xsk->app_stats.tx_wakeup_sendtos - xsk->app_stats_prev.tx_wakeup_sendtos) * 1000000000. / diff;
	opt_polls_ps = (xsk->app_stats.opt_polls - xsk->app_stats_prev.opt_polls) * 1000000000. / diff;

	printf("\n%-18s %-14s %-14s\n", "", "calls/s", "count");
	printf("%-18s %'-14.0f %'-14lu\n", "rx empty polls", rx_empty_polls_ps, xsk->app_stats.rx_empty_polls);
	printf("%-18s %'-14.0f %'-14lu\n", "fill fail polls", fill_fail_polls_ps, xsk->app_stats.fill_fail_polls);
	printf("%-18s %'-14.0f %'-14lu\n", "copy tx sendtos", copy_tx_sendtos_ps, xsk->app_stats.copy_tx_sendtos);
	printf("%-18s %'-14.0f %'-14lu\n", "backpressure", backpressure_sleep_ps, xsk->app_stats.backpressure);
	printf("%-18s %'-14.0f %'-14lu\n", "tx wakeup sendtos", tx_wakeup_sendtos_ps, xsk->app_stats.tx_wakeup_sendtos);
	printf("%-18s %'-14.0f %'-14lu\n", "opt polls", opt_polls_ps, xsk->app_stats.opt_polls);

	xsk->app_stats_prev.rx_empty_polls = xsk->app_stats.rx_empty_polls;
	xsk->app_stats_prev.fill_fail_polls = xsk->app_stats.fill_fail_polls;
	xsk->app_stats_prev.copy_tx_sendtos = xsk->app_stats.copy_tx_sendtos;
	xsk->app_stats_prev.backpressure = xsk->app_stats.backpressure;
	xsk->app_stats_prev.tx_wakeup_sendtos = xsk->app_stats.tx_wakeup_sendtos;
	xsk->app_stats_prev.opt_polls = xsk->app_stats.opt_polls;
}

static void __dump_driver_stats(struct config *cfg, struct socket *xsk, long diff)
{
	double intrs_ps;
	int n_ints = get_irqs(cfg);

	if (n_ints < 0) {
		log_error("error getting intr info for intr %i\n", cfg->irq_no);
		return;
	}
	xsk->drv_stats.intrs = n_ints - cfg->irqs_at_init;

	intrs_ps = (xsk->drv_stats.intrs - xsk->drv_stats_prev.intrs) * 1000000000. / diff;

	printf("\n%-18s %-14s %-14s\n", "", "intrs/s", "count");
	printf("%-18s %'-14.0f %'-14lu\n", "irqs", intrs_ps, xsk->drv_stats.intrs);

	xsk->drv_stats_prev.intrs = xsk->drv_stats.intrs;
}

void flash__dump_stats(struct config *cfg, struct socket *xsk)
{
	size_t now = flash__get_nsecs(cfg);
	long diff = now - xsk->timestamp;
	const char *setup_str = "FLASH";

	xsk->timestamp = now;

	double rx_pps, tx_pps, dx_pps, dropped_pps, rx_invalid_pps, full_pps, fill_empty_pps, tx_invalid_pps, tx_empty_pps;

	rx_pps = (xsk->ring_stats.rx_npkts - xsk->ring_stats_prev.rx_npkts) * 1000000000. / diff;
	tx_pps = (xsk->ring_stats.tx_npkts - xsk->ring_stats_prev.tx_npkts) * 1000000000. / diff;
	dx_pps = (xsk->ring_stats.drop_npkts - xsk->ring_stats_prev.drop_npkts) * 1000000000. / diff;

	printf("%c %s:%d %s ", spinner[spinner_index++ & 3], cfg->ifname, xsk->ifqueue, setup_str);
	if (cfg->xsk->xdp_flags & XDP_FLAGS_SKB_MODE)
		printf("xdp-skb ");
	else if (cfg->xsk->xdp_flags & XDP_FLAGS_DRV_MODE)
		printf("xdp-drv ");
	else
		printf("	");

	if (cfg->xsk->mode & FLASH__POLL)
		printf("poll() ");
	if (cfg->xsk->mode & FLASH__BUSY_POLL) {
		if (cfg->smart_poll)
			printf("busy-poll | smart-poll ");
		else
			printf("busy-poll ");
	}

	printf("\n");

	if (cfg->frags_enabled) {
		uint64_t rx_frags = xsk->ring_stats.rx_frags;
		uint64_t tx_frags = xsk->ring_stats.tx_frags;
		double rx_fps = (rx_frags - xsk->ring_stats_prev.rx_frags) * 1000000000. / diff;
		double tx_fps = (tx_frags - xsk->ring_stats_prev.tx_frags) * 1000000000. / diff;

		printf("%-18s %-14s %-14s %-14s %-14s %-14.2f\n", "", "pps", "pkts", "fps", "frags", diff / 1000000000.);
		printf("%-18s %'-14.0f %'-14lu %'-14.0f %'-14lu\n", "rx", rx_pps, xsk->ring_stats.rx_npkts, rx_fps, rx_frags);
		printf("%-18s %'-14.0f %'-14lu %'-14.0f %'-14lu\n", "tx", tx_pps, xsk->ring_stats.tx_npkts, tx_fps, tx_frags);
		xsk->ring_stats_prev.rx_frags = rx_frags;
		xsk->ring_stats_prev.tx_frags = tx_frags;
	} else {
		printf("%-18s %-14s %-14s %-14.2f\n", "", "pps", "pkts", diff / 1000000000.);
		printf("%-18s %'-14.0f %'-14lu\n", "rx", rx_pps, xsk->ring_stats.rx_npkts);
		printf("%-18s %'-14.0f %'-14lu\n", "tx", tx_pps, xsk->ring_stats.tx_npkts);
		printf("%-18s %'-14.0f %'-14lu\n", "drop", dx_pps, xsk->ring_stats.drop_npkts);
	}

	xsk->ring_stats_prev.rx_npkts = xsk->ring_stats.rx_npkts;
	xsk->ring_stats_prev.tx_npkts = xsk->ring_stats.tx_npkts;
	xsk->ring_stats_prev.drop_npkts = xsk->ring_stats.drop_npkts;

	if (cfg->extra_stats) {
		if (!__xsk_get_xdp_stats(xsk->fd, xsk)) {
			dropped_pps = (xsk->ring_stats.rx_dropped_npkts - xsk->ring_stats_prev.rx_dropped_npkts) * 1000000000. / diff;
			rx_invalid_pps =
				(xsk->ring_stats.rx_invalid_npkts - xsk->ring_stats_prev.rx_invalid_npkts) * 1000000000. / diff;
			tx_invalid_pps =
				(xsk->ring_stats.tx_invalid_npkts - xsk->ring_stats_prev.tx_invalid_npkts) * 1000000000. / diff;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
			full_pps = (xsk->ring_stats.rx_full_npkts - xsk->ring_stats_prev.rx_full_npkts) * 1000000000. / diff;
			fill_empty_pps =
				(xsk->ring_stats.rx_fill_empty_npkts - xsk->ring_stats_prev.rx_fill_empty_npkts) * 1000000000. / diff;
			tx_empty_pps = (xsk->ring_stats.tx_empty_npkts - xsk->ring_stats_prev.tx_empty_npkts) * 1000000000. / diff;
#endif
			printf("%-18s %'-14.0f %'-14lu\n", "rx dropped", dropped_pps, xsk->ring_stats.rx_dropped_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "rx invalid", rx_invalid_pps, xsk->ring_stats.rx_invalid_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "tx invalid", tx_invalid_pps, xsk->ring_stats.tx_invalid_npkts);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
			printf("%-18s %'-14.0f %'-14lu\n", "rx queue full", full_pps, xsk->ring_stats.rx_full_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "fill ring empty", fill_empty_pps, xsk->ring_stats.rx_fill_empty_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "tx ring empty", tx_empty_pps, xsk->ring_stats.tx_empty_npkts);
#endif
			xsk->ring_stats_prev.rx_dropped_npkts = xsk->ring_stats.rx_dropped_npkts;
			xsk->ring_stats_prev.rx_invalid_npkts = xsk->ring_stats.rx_invalid_npkts;
			xsk->ring_stats_prev.tx_invalid_npkts = xsk->ring_stats.tx_invalid_npkts;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 9, 0)
			xsk->ring_stats_prev.rx_full_npkts = xsk->ring_stats.rx_full_npkts;
			xsk->ring_stats_prev.rx_fill_empty_npkts = xsk->ring_stats.rx_fill_empty_npkts;
			xsk->ring_stats_prev.tx_empty_npkts = xsk->ring_stats.tx_empty_npkts;
#endif
		} else {
			printf("%-15s\n", "Error retrieving extra stats");
		}
	}

	if (cfg->app_stats) {
		__dump_app_stats(xsk, diff);
	}
	if (cfg->irq_no) {
		__dump_driver_stats(cfg, xsk, diff);
	}
}

void *flash__stats_thread(void *conf)
{
	struct stats_conf *arg = (struct stats_conf *)conf;
	struct nf *nf = arg->nf;
	struct config *cfg = arg->cfg;

	if (cfg->verbose) {
		unsigned int interval = cfg->stats_interval;
		setlocale(LC_ALL, "");

		for (int i = 0; i < cfg->total_sockets; i++)
			nf->thread[i]->socket->timestamp = flash__get_nsecs(cfg);

		while (!done) {
			sleep(interval);
			if (system("clear") != 0)
				log_error("Terminal clear error");
			for (int i = 0; i < cfg->total_sockets; i++) {
				flash__dump_stats(cfg, nf->thread[i]->socket);
			}
		}
	}
	return NULL;
}
