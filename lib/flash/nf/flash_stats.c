#include <log.h>
#include <limits.h>

#include "flash_nf.h"

static int get_irqs(struct config *cfg)
{
	char count_path[PATH_MAX];
	int total_intrs = -1;
	FILE *f_count_proc;
	char line[4096];

	snprintf(count_path, sizeof(count_path),
		 "/sys/kernel/irq/%i/per_cpu_count", cfg->irq_no);
	f_count_proc = fopen(count_path, "r");
	if (f_count_proc == NULL) {
		printf("Failed to open %s\n", count_path);
		return total_intrs;
	}

	if (fgets(line, sizeof(line), f_count_proc) == NULL ||
	    line[strlen(line) - 1] != '\n') {
		printf("Error reading from %s\n", count_path);
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

unsigned long flash__get_nsecs(struct config *cfg)
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
		xsk->ring_stats.rx_full_npkts = stats.rx_ring_full;
		xsk->ring_stats.rx_fill_empty_npkts =
			stats.rx_fill_ring_empty_descs;
		xsk->ring_stats.tx_empty_npkts = stats.tx_ring_empty_descs;
		return 0;
	}

	return -EINVAL;
}

static void __dump_app_stats(struct socket *xsk, long diff)
{
	double rx_empty_polls_ps, fill_fail_polls_ps, copy_tx_sendtos_ps,
		tx_wakeup_sendtos_ps, opt_polls_ps;

	rx_empty_polls_ps = (xsk->app_stats.rx_empty_polls -
			     xsk->app_stats.prev_rx_empty_polls) *
			    1000000000. / diff;
	fill_fail_polls_ps = (xsk->app_stats.fill_fail_polls -
			      xsk->app_stats.prev_fill_fail_polls) *
			     1000000000. / diff;
	copy_tx_sendtos_ps = (xsk->app_stats.copy_tx_sendtos -
			      xsk->app_stats.prev_copy_tx_sendtos) *
			     1000000000. / diff;
	tx_wakeup_sendtos_ps = (xsk->app_stats.tx_wakeup_sendtos -
				xsk->app_stats.prev_tx_wakeup_sendtos) *
			       1000000000. / diff;
	opt_polls_ps =
		(xsk->app_stats.opt_polls - xsk->app_stats.prev_opt_polls) *
		1000000000. / diff;

	printf("\n%-18s %-14s %-14s\n", "", "calls/s", "count");
	printf("%-18s %'-14.0f %'-14lu\n", "rx empty polls", rx_empty_polls_ps,
	       xsk->app_stats.rx_empty_polls);
	printf("%-18s %'-14.0f %'-14lu\n", "fill fail polls",
	       fill_fail_polls_ps, xsk->app_stats.fill_fail_polls);
	printf("%-18s %'-14.0f %'-14lu\n", "copy tx sendtos",
	       copy_tx_sendtos_ps, xsk->app_stats.copy_tx_sendtos);
	printf("%-18s %'-14.0f %'-14lu\n", "tx wakeup sendtos",
	       tx_wakeup_sendtos_ps, xsk->app_stats.tx_wakeup_sendtos);
	printf("%-18s %'-14.0f %'-14lu\n", "opt polls", opt_polls_ps,
	       xsk->app_stats.opt_polls);

	xsk->app_stats.prev_rx_empty_polls = xsk->app_stats.rx_empty_polls;
	xsk->app_stats.prev_fill_fail_polls = xsk->app_stats.fill_fail_polls;
	xsk->app_stats.prev_copy_tx_sendtos = xsk->app_stats.copy_tx_sendtos;
	xsk->app_stats.prev_tx_wakeup_sendtos =
		xsk->app_stats.tx_wakeup_sendtos;
	xsk->app_stats.prev_opt_polls = xsk->app_stats.opt_polls;
}

static void __dump_driver_stats(struct config *cfg, struct socket *xsk,
				long diff)
{
	double intrs_ps;
	int n_ints = get_irqs(cfg);

	if (n_ints < 0) {
		printf("error getting intr info for intr %i\n", cfg->irq_no);
		return;
	}
	xsk->drv_stats.intrs = n_ints - cfg->irqs_at_init;

	intrs_ps = (xsk->drv_stats.intrs - xsk->drv_stats.prev_intrs) *
		   1000000000. / diff;

	printf("\n%-18s %-14s %-14s\n", "", "intrs/s", "count");
	printf("%-18s %'-14.0f %'-14lu\n", "irqs", intrs_ps,
	       xsk->drv_stats.intrs);

	xsk->drv_stats.prev_intrs = xsk->drv_stats.intrs;
}

void flash__dump_stats(struct config *cfg, struct socket *xsk, int flags)
{
	unsigned long now = flash__get_nsecs(cfg);
	long diff = now - xsk->timestamp;
	const char *setup_str = "INVALID";
	const char *backp_str = "BACKP_DISABLED";

	xsk->timestamp = now;

	double rx_pps, tx_pps, dropped_pps, rx_invalid_pps, full_pps,
		fill_empty_pps, tx_invalid_pps, tx_empty_pps;

	rx_pps = (xsk->ring_stats.rx_npkts - xsk->ring_stats.prev_rx_npkts) *
		 1000000000. / diff;
	tx_pps = (xsk->ring_stats.tx_npkts - xsk->ring_stats.prev_tx_npkts) *
		 1000000000. / diff;

	if (flags & FLASH__RXTX)
		setup_str = "fwd";
	else if (flags & FLASH__RX)
		setup_str = "rxdrop";

	if (flags & FLASH__BACKP)
		backp_str = "backpressure";

	printf("ifname:queue no. %s %s ", setup_str, backp_str);
	// printf("%s:%d %s %s ", cfg->ifname, cfg->ifqueue[i], setup_str,
	    //    backp_str);
	// if (cfg->attach_mode == XDP_MODE_SKB)
	// 	printf("xdp-skb ");
	// else if (cfg->attach_mode == XDP_MODE_NATIVE)
	// 	printf("xdp-drv ");
	// else
	printf("	");

	// if (cfg->xsk->mode__poll)
	// 	printf("poll() ");
	printf("\n");

	if (cfg->frags_enabled) {
		__u64 rx_frags = xsk->ring_stats.rx_frags;
		__u64 tx_frags = xsk->ring_stats.tx_frags;
		double rx_fps = (rx_frags - xsk->ring_stats.prev_rx_frags) *
				1000000000. / diff;
		double tx_fps = (tx_frags - xsk->ring_stats.prev_tx_frags) *
				1000000000. / diff;

		printf("%-18s %-14s %-14s %-14s %-14s %-14.2f\n", "", "pps",
		       "pkts", "fps", "frags", diff / 1000000000.);
		printf("%-18s %'-14.0f %'-14lu %'-14.0f %'-14llu\n", "rx",
		       rx_pps, xsk->ring_stats.rx_npkts, rx_fps, rx_frags);
		printf("%-18s %'-14.0f %'-14lu %'-14.0f %'-14llu\n", "tx",
		       tx_pps, xsk->ring_stats.tx_npkts, tx_fps, tx_frags);
		xsk->ring_stats.prev_rx_frags = rx_frags;
		xsk->ring_stats.prev_tx_frags = tx_frags;
	} else {
		printf("%-18s %-14s %-14s %-14.2f\n", "", "pps", "pkts",
		       diff / 1000000000.);
		printf("%-18s %'-14.0f %'-14lu\n", "rx", rx_pps,
		       xsk->ring_stats.rx_npkts);
		printf("%-18s %'-14.0f %'-14lu\n", "tx", tx_pps,
		       xsk->ring_stats.tx_npkts);
	}

	xsk->ring_stats.prev_rx_npkts = xsk->ring_stats.rx_npkts;
	xsk->ring_stats.prev_tx_npkts = xsk->ring_stats.tx_npkts;

	if (cfg->extra_stats) {
		if (!__xsk_get_xdp_stats(xsk->fd, xsk)) {
			dropped_pps = (xsk->ring_stats.rx_dropped_npkts -
				       xsk->ring_stats.prev_rx_dropped_npkts) *
				      1000000000. / diff;
			rx_invalid_pps =
				(xsk->ring_stats.rx_invalid_npkts -
				 xsk->ring_stats.prev_rx_invalid_npkts) *
				1000000000. / diff;
			tx_invalid_pps =
				(xsk->ring_stats.tx_invalid_npkts -
				 xsk->ring_stats.prev_tx_invalid_npkts) *
				1000000000. / diff;
			full_pps = (xsk->ring_stats.rx_full_npkts -
				    xsk->ring_stats.prev_rx_full_npkts) *
				   1000000000. / diff;
			fill_empty_pps =
				(xsk->ring_stats.rx_fill_empty_npkts -
				 xsk->ring_stats.prev_rx_fill_empty_npkts) *
				1000000000. / diff;
			tx_empty_pps = (xsk->ring_stats.tx_empty_npkts -
					xsk->ring_stats.prev_tx_empty_npkts) *
				       1000000000. / diff;

			printf("%-18s %'-14.0f %'-14lu\n", "rx dropped",
			       dropped_pps, xsk->ring_stats.rx_dropped_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "rx invalid",
			       rx_invalid_pps,
			       xsk->ring_stats.rx_invalid_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "tx invalid",
			       tx_invalid_pps,
			       xsk->ring_stats.tx_invalid_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "rx queue full",
			       full_pps, xsk->ring_stats.rx_full_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "fill ring empty",
			       fill_empty_pps,
			       xsk->ring_stats.rx_fill_empty_npkts);
			printf("%-18s %'-14.0f %'-14lu\n", "tx ring empty",
			       tx_empty_pps, xsk->ring_stats.tx_empty_npkts);

			xsk->ring_stats.prev_rx_dropped_npkts =
				xsk->ring_stats.rx_dropped_npkts;
			xsk->ring_stats.prev_rx_invalid_npkts =
				xsk->ring_stats.rx_invalid_npkts;
			xsk->ring_stats.prev_tx_invalid_npkts =
				xsk->ring_stats.tx_invalid_npkts;
			xsk->ring_stats.prev_rx_full_npkts =
				xsk->ring_stats.rx_full_npkts;
			xsk->ring_stats.prev_rx_fill_empty_npkts =
				xsk->ring_stats.rx_fill_empty_npkts;
			xsk->ring_stats.prev_tx_empty_npkts =
				xsk->ring_stats.tx_empty_npkts;
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