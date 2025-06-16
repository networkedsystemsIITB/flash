/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <flash_defines.h>
#include <log.h>
#include <limits.h>

#include "flash_params.h"

#define BUFSIZE 30

const struct option_wrapper long_options[] = {

	{ { "help", no_argument, NULL, 'h' }, "Show help", false },

	{ { "umem-id", required_argument, NULL, 'u' }, "umem id to connect to monitor", "<umem-id>", true },

	{ { "nf-id", required_argument, NULL, 'f' }, "nf id to connect to monitor", "<nf-id>", true },

	{ { "tx-first", no_argument, NULL, 't' }, "TX without receiving any packets [default: disabled]" },

	{ { "app-stats", no_argument, NULL, 'a' }, "Display application (syscall) statistics [default: disabled]" },

	{ { "extra-stats", no_argument, NULL, 'x' }, "Display extra (xdp) statistics [default: disabled]" },

	{ { "interval", required_argument, NULL, 'n' }, "Specify statistics update interval [default: 1 sec]", "<interval>" },

	{ { "quiet", no_argument, NULL, 'Q' }, "Quiet mode (no output) [default: disabled]" },

	{ { "smart-poll", no_argument, NULL, 'p' }, "Smart polling mode [default: disabled]" },

	{ { "idle-timeout", required_argument, NULL, 'i' }, "Idle timeout for smart polling mode in ms [default: 100]", "<val>" },

	{ { "idleness", required_argument, NULL, 'I' },
	  "Idleness for smart polling, busy-polling (0) to poll (1) [default: 0]",
	  "<idleness>" },

	{ { "timeout", required_argument, NULL, 'b' }, "Sleep duration on backpressure in us [default: 1000]", "<timeout>" },

	{ { "bp-sense", required_argument, NULL, 'B' },
	  "Sensitivity for detecting backpressure, 0: 0 pkts - 1: 2048 pkts [default: 0.5]",
	  "<val>" },

	{ { "frags", no_argument, NULL, 'F' }, "Enable frags (multi-buffer) support -- not implemented yet", false },

	{ { "clock", required_argument, NULL, 'w' }, "Clock NAME (default MONOTONIC) -- not implemented yet", "<clock>", false },

	{ { 0, 0, NULL, 0 }, NULL, false }
};

static int get_clockid(clockid_t *id, const char *name)
{
	const struct clockid_map *clk;

	for (clk = clockids_map; clk->name; clk++) {
		if (strcasecmp(clk->name, name) == 0) {
			*id = clk->clockid;
			return 0;
		}
	}

	return -1;
}

static int option_wrappers_to_options(const struct option_wrapper *wrapper, struct option **options)
{
	int i, num;
	struct option *new_options;
	for (i = 0; wrapper[i].option.name != 0; i++) {
	}
	num = i;

	new_options = malloc(sizeof(struct option) * num);
	if (!new_options)
		return -1;
	for (i = 0; i < num; i++) {
		memcpy(&new_options[i], &wrapper[i], sizeof(struct option));
	}

	*options = new_options;
	return 0;
}

static void _print_options(const struct option_wrapper *long_options, bool required)
{
	int i, pos;
	char buf[BUFSIZE];

	for (i = 0; long_options[i].option.name != 0; i++) {
		if (long_options[i].required != required)
			continue;

		if (long_options[i].option.val > 64) /* ord('A') = 65 */
			printf(" -%c,", long_options[i].option.val);
		else
			printf("    ");
		pos = snprintf(buf, BUFSIZE, " --%s", long_options[i].option.name);
		if (long_options[i].metavar)
			snprintf(&buf[pos], BUFSIZE - pos, " %s", long_options[i].metavar);
		printf("%-22s", buf);
		printf("  %s", long_options[i].help);
		printf("\n");
	}
}

static void usage(const char *prog_name, const struct option_wrapper *long_options, bool full, struct config *cfg)
{
	printf("\nUsage: %s [options] -- [app-options]\n", prog_name);

	if (!full) {
		printf("Use --help (or -h) to see full option list.\n");
		return;
	}

	printf("\n");
	if (cfg->app_name)
		printf("%s using FLASH AF_XDP Library\n\n", cfg->app_name);
	else
		printf("FLASH AF_XDP Library\n\n");
	printf("Required options:\n");
	_print_options(long_options, true);
	printf("\n");
	printf("Other options:\n");
	_print_options(long_options, false);
	printf("\n");
	if (cfg->app_options) {
		printf("Application options:\n");
		for (int i = 0; cfg->app_options[i]; i++) {
			printf(" %s\n", cfg->app_options[i]);
		}
		printf("\n");
	}

	printf("For more help on how to use FLASH, head to https://github.com/networkedsystemsIITB/flash\n\n");
}

static int parse_cmdline_args(int argc, char **argv, const struct option_wrapper *options_wrapper, struct config *cfg)
{
	int opt, ret;
	int longindex = 0;
	struct option *long_options;
	bool full_help = false;

	const int old_optind = optind;
	const int old_optopt = optopt;
	char *const old_optarg = optarg;

	optind = 1;

	if (option_wrappers_to_options(options_wrapper, &long_options)) {
		log_error("ERROR: (Parsing error) Unable to malloc()");
		return -1;
	}

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "u:f:taxn:Qpi:I:b:B:Fw:h", long_options, &longindex)) != -1) {
		switch (opt) {
		case 'u':
			cfg->umem_id = atoi(optarg);
			break;
		case 'f':
			cfg->nf_id = atoi(optarg);
			break;
		case 't':
			cfg->rx_first = false;
			break;
		case 'a':
			cfg->app_stats = true;
			break;
		case 'x':
			cfg->extra_stats = true;
			break;
		case 'n':
			cfg->stats_interval = atoi(optarg);
			break;
		case 'Q':
			cfg->verbose = false;
			break;
		case 'p':
			cfg->smart_poll = true;
			break;
		case 'i':
			cfg->xsk->idle_timeout = atoi(optarg);
			break;
		case 'I':
			cfg->xsk->idle_thres = (__u32)(atof(optarg) * cfg->xsk->batch_size);
			break;
		case 'b':
			cfg->xsk->bp_timeout = atoi(optarg);
			break;
		case 'B':
			cfg->xsk->bp_thres = (__u32)(atof(optarg) * XSK_RING_PROD__DEFAULT_NUM_DESCS);
			break;
		case 'F':
			cfg->frags_enabled = true;
			break;
		case 'w':
			if (get_clockid(&cfg->clock, optarg))
				log_warn("ERROR: Invalid clock %s. Default to CLOCK_MONOTONIC.", optarg);
			break;
		case 'h':
			full_help = true;
			/* fall-through */
		default:
			usage(argv[0], options_wrapper, full_help, cfg);
			free(long_options);
			return -1;
		}
	}
	free(long_options);

	/* Check for required options */
	if (cfg->umem_id < 0 || cfg->nf_id < 0) {
		log_fatal("ERROR: (Parsing error) Required options missing: --umem-id and --nf-id");
		usage(argv[0], options_wrapper, (argc == 1), cfg);
		return -1;
	}

	if (optind >= 0)
		argv[optind - 1] = argv[0];

	ret = optind - 1;

	/* restore getopt lib */
	optind = old_optind;
	optopt = old_optopt;
	optarg = old_optarg;
	return ret;
}

int flash__parse_cmdline_args(int argc, char **argv, struct config *cfg)
{
	int ret;

	if (!cfg) {
		log_error("ERROR: (Parsing error) NULL config pointer");
		return -1;
	}

	cfg->umem = calloc(1, sizeof(struct umem_config));
	cfg->xsk = calloc(1, sizeof(struct xsk_config));
	if (!cfg->xsk || !cfg->umem) {
		log_error("ERROR: Memory allocation failed");
		return -1;
	}

	cfg->umem_id = -1;
	cfg->nf_id = -1;
	cfg->rx_first = true;
	cfg->xsk->batch_size = BATCH_SIZE;
	cfg->umem->frame_size = FRAME_SIZE;
	cfg->stats_interval = 1;
	cfg->app_stats = false;
	cfg->extra_stats = false;
	cfg->verbose = true;
	cfg->smart_poll = false;
	cfg->xsk->idle_timeout = 100;
	cfg->xsk->poll_timeout = -1;
	cfg->xsk->idle_thres = 0;
	cfg->xsk->bp_timeout = 1000;
	cfg->xsk->bp_thres = (__u32)(XSK_RING_PROD__DEFAULT_NUM_DESCS * 0.5);

	ret = parse_cmdline_args(argc, argv, long_options, cfg);
	if (ret < 0)
		goto cleanup;

	if ((cfg->umem->frame_size & (cfg->umem->frame_size - 1))) {
		log_error("ERROR: (Parsing error) --frame-size=%d is not a power of two", cfg->umem->frame_size);
		goto cleanup;
	}

	return ret;

cleanup:
	free(cfg->umem);
	free(cfg->xsk);
	return -1;
}
