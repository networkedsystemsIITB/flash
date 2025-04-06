/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <flash_defines.h>
#include <log.h>
#include <limits.h>

#include "flash_params.h"

#define BUFSIZE 30

const char *__doc__ = "FLASH AF_XDP NF Library\n";

const struct option_wrapper long_options[] = {

	{ { "umem-id", required_argument, NULL, 'u' }, "Umem id to connect to monitor" },

	{ { "nf-id", required_argument, NULL, 'f' }, "NF id to connect to monitor" },

	{ { "app-stats", no_argument, NULL, 'a' }, "Display application (syscall) statistics. (default: disabled)" },

	{ { "extra-stats", no_argument, NULL, 'x' }, "Display extra (xdp) statistics. (default: disabled)" },

	{ { "interval", required_argument, NULL, 'n' }, "Specify statistics update interval (default: 1 sec)." },

	{ { "quiet", no_argument, NULL, 'Q' }, "Quiet mode (no output) (default: disabled)" },

	{ { "smart-poll", no_argument, NULL, 'p' }, "Smart polling mode (default: disabled)" },

	{ { "idle-timeout", required_argument, NULL, 'i' }, "Idle timeout for smart polling mode in ms. (default: 100)" },

	{ { "idleness", required_argument, NULL, 'I' }, "Idleness for smart polling, busy-polling (0) to poll (1) (default: 0)" },

	{ { "bp-timeout", required_argument, NULL, 'b' }, "Sleep duration on backpressure in us (default: 1000)" },

	{ { "bp-sense", required_argument, NULL, 'B' },
	  "Sensitivity for detecting backpressure, 0: 0 pkts - 1: 2048 pkts (default: 0.5)" },

	{ { "frags", no_argument, NULL, 'F' }, "Enable frags (multi-buffer) support. -- not implemented yet" },

	{ { "clock", required_argument, NULL, 'w' }, "Clock NAME (default MONOTONIC). -- not implemented yet" },

	{ { "help", no_argument, NULL, 'h' }, "Show help", false },

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

static void usage(const char *prog_name, const char *doc, const struct option_wrapper *long_options, bool full)
{
	printf("Usage: %s [options]\n", prog_name);

	if (!full) {
		printf("Use --help (or -h) to see full option list.\n");
		return;
	}

	printf("\nDOCUMENTATION:\n %s\n", doc);
	printf("Required options:\n");
	_print_options(long_options, true);
	printf("\n");
	printf("Other options:\n");
	_print_options(long_options, false);
	printf("\n");
}

static int parse_cmdline_args(int argc, char **argv, const struct option_wrapper *options_wrapper, struct config *cfg, const char *doc)
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
		log_error("ERROR: (Parsing error) Unable to malloc()\n");
		exit(EXIT_FAILURE);
	}

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "u:f:axn:Qpi:I:b:B:Fw:h", long_options, &longindex)) != -1) {
		switch (opt) {
		case 'u':
			cfg->umem_id = atoi(optarg);
			break;
		case 'f':
			cfg->nf_id = atoi(optarg);
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
				log_error("ERROR: Invalid clock %s. Default to CLOCK_MONOTONIC.\n", optarg);
			break;
		case 'h':
			full_help = true;
			/* fall-through */
		default:
			usage(argv[0], doc, options_wrapper, full_help);
			free(long_options);
			exit(EXIT_FAILURE);
		}
	}
	free(long_options);

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
	cfg->umem = calloc(1, sizeof(struct umem_config));
	cfg->xsk = calloc(1, sizeof(struct xsk_config));
	if (!cfg->xsk || !cfg->umem) {
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

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

	int ret = parse_cmdline_args(argc, argv, long_options, cfg, __doc__);

	if ((cfg->umem->frame_size & (cfg->umem->frame_size - 1))) {
		log_error("ERROR: (Parsing error) --frame-size=%d is not a power of two\n", cfg->umem->frame_size);
		exit(EXIT_FAILURE);
	}

	return ret;
}
