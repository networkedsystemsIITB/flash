/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <flash_defines.h>
#include <log.h>
#include <limits.h>

#include "flash_params.h"

#define BUFSIZE 30

const char *__doc__ = "AF_XDP NF Library\n";

const struct option_wrapper long_options[] = {

	{ { "app-stats", no_argument, NULL, 'a' }, "Display application (syscall) statistics." },

	{ { "extra-stats", no_argument, NULL, 'x' }, "Display extra statistics." },

	{ { "interval", required_argument, NULL, 'n' }, "Specify statistics update interval (default 1 sec)." },

	{ { "nf_id", required_argument, NULL, 'f' }, "NF id to connect to monitor" },

	{ { "umem_id", required_argument, NULL, 'u' }, "Umem id to connect to monitor" },

	{ { "quiet", no_argument, NULL, 'Q' }, "Quiet mode (no output)" },

	{ { "clock", required_argument, NULL, 'w' }, "Clock NAME (default MONOTONIC). -- not implemented yet" },

	{ { "frags", no_argument, NULL, 'F' }, "Enable frags (multi-buffer) support" },

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
		log_info("Parsing command line arguments3");

		log_error("ERROR: (Parsing error) Unable to malloc()\n");
		exit(EXIT_FAILURE);
	}

	/* Parse commands line args */
	while ((opt = getopt_long(argc, argv, "axhFQn:w:u:f:", long_options, &longindex)) != -1) {
		switch (opt) {
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
		case 'u':
			cfg->umem_id = atoi(optarg);
			break;
		case 'f':
			cfg->nf_id = atoi(optarg);
			break;
		case 'w':
			if (get_clockid(&cfg->clock, optarg))
				log_error("ERROR: Invalid clock %s. Default to CLOCK_MONOTONIC.\n", optarg);
			break;
		case 'F':
			cfg->frags_enabled = true;
			break;
		case 'h':
			full_help = true;
			/* fall-through */
		default:
			usage(argv[0], doc, options_wrapper, full_help);
			log_info("Parsing command line arguments4");
			free(long_options);
			exit(EXIT_FAILURE);
		}
	}
	free(long_options);

	if (optind >= 0)
		argv[optind - 1] = argv[0];

	ret = optind - 1;
	log_info("Parsing command line arguments5");

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
	cfg->total_sockets = -1;
	cfg->stats_interval = 1;
	cfg->app_stats = false;
	cfg->extra_stats = false;
	cfg->frags_enabled = false;
	cfg->custom_xsk = false;
	cfg->verbose = true;
	cfg->xsk->bind_flags &= ~XDP_COPY;
	cfg->xsk->bind_flags |= XDP_ZEROCOPY;
	cfg->xsk->mode__busy_poll = true;
	cfg->xsk->mode__zero_copy = true;

	int ret = parse_cmdline_args(argc, argv, long_options, cfg, __doc__);

	log_info("Parsing command line arguments2");

	if ((cfg->umem->frame_size & (cfg->umem->frame_size - 1))) {
		log_error("ERROR: (Parsing error) --frame-size=%d is not a power of two\n", cfg->umem->frame_size);
		exit(EXIT_FAILURE);
	}

	return ret;
}
