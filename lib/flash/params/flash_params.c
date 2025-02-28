/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <flash_defines.h>
#include <log.h>
#include <limits.h>

#include "flash_params.h"

#define BUFSIZE 30

const char *__doc__ = "AF_XDP NF Library\n";

static const char *opt_irq_str = "";

const struct option_wrapper long_options[] = {

	{ { "zero-copy", no_argument, NULL, 'z' }, "Force zero-copy mode" },

	{ { "poll-mode", no_argument, NULL, 'p' },
	  "Use the poll() API waiting for packets to arrive" },

	{ { "is_primary", no_argument, NULL, 'P' },
	  "Primary NF should send signal for creating a UMEM to monitor",
	  "<total-sockets>",
	  true },

	{ { "thread-count", required_argument, NULL, 't' },
	  "The number of threads(sockets) for this NF",
	  "<thread-count>",
	  true },

	{ { "busypoll-mode", no_argument, NULL, 'b' },
	  "Use the syscall busy poll with interrupts disabled" },

	{ { "no-need-wakeup", no_argument, NULL, 'm' },
	  "Turn off use of driver need wakeup flag." },

	{ { "interface", required_argument, NULL, 'i' },
	  "Operate on device <ifname>",
	  "<ifname>",
	  true },

	{ { "queue", required_argument, NULL, 'q' },
	  "Configure interface receive queue for AF_XDP, default=0",
	  "<queue-id>",
	  true },

	{ { "app-stats", no_argument, NULL, 'a' },
	  "Display application (syscall) statistics." },

	{ { "extra-stats", no_argument, NULL, 'x' },
	  "Display extra statistics." },

	{ { "irq-string", required_argument, NULL, 'I' },
	  "Display driver interrupt statistics for interface associated with irq-string.",
	  "<irq>" },

	{ { "interval", required_argument, NULL, 'n' },
	  "Specify statistics update interval (default 1 sec)." },

	{ { "quiet", no_argument, NULL, 'Q' }, "Quiet mode (no output)" },

	{ { "clock", required_argument, NULL, 'w' },
	  "Clock NAME (default MONOTONIC). -- not implemented yet" },

	{ { "frags", no_argument, NULL, 'F' },
	  "Enable frags (multi-buffer) support" },

	{ { "help", no_argument, NULL, 'h' }, "Show help", false },

	{ { 0, 0, NULL, 0 }, NULL, false }
};

static int *get_ifqueues(__u32 queue_mask, int n_threads)
{
	log_info("QUEUE MASK: %d", queue_mask);
	int *ifqueue = calloc(n_threads, sizeof(int));
	int count = 0, thread = 0;
	while (queue_mask != 0) {
		if ((queue_mask & (1 << 0)) == 1)
			ifqueue[thread++] = count;
		queue_mask >>= 1;
		count++;
	}
	return ifqueue;
}

static bool get_interrupt_number(struct config *cfg)
{
	FILE *f_int_proc;
	char line[4096];
	bool found = false;

	f_int_proc = fopen("/proc/interrupts", "r");
	if (f_int_proc == NULL) {
		printf("Failed to open /proc/interrupts.\n");
		return found;
	}

	while (!feof(f_int_proc) && !found) {
		/* Make sure to read a full line at a time */
		if (fgets(line, sizeof(line), f_int_proc) == NULL ||
		    line[strlen(line) - 1] != '\n') {
			printf("Error reading from interrupts file\n");
			break;
		}

		/* Extract interrupt number from line */
		if (strstr(line, opt_irq_str) != NULL) {
			cfg->irq_no = atoi(line);
			found = true;
			break;
		}
	}

	fclose(f_int_proc);

	return found;
}

int get_irqs(struct config *cfg)
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

static int option_wrappers_to_options(const struct option_wrapper *wrapper,
				      struct option **options)
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

static void _print_options(const struct option_wrapper *long_options,
			   bool required)
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
		pos = snprintf(buf, BUFSIZE, " --%s",
			       long_options[i].option.name);
		if (long_options[i].metavar)
			snprintf(&buf[pos], BUFSIZE - pos, " %s",
				 long_options[i].metavar);
		printf("%-22s", buf);
		printf("  %s", long_options[i].help);
		printf("\n");
	}
}

static void usage(const char *prog_name, const char *doc,
		  const struct option_wrapper *long_options, bool full)
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

static int parse_cmdline_args(int argc, char **argv,
			      const struct option_wrapper *options_wrapper,
			      struct config *cfg, const char *doc)
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
	while ((opt = getopt_long(argc, argv, "axhzpbFQmP:t:i:q:I:n:w:",
				  long_options, &longindex)) != -1) {
		switch (opt) {
		case 'z':
			cfg->xsk->bind_flags &= ~XDP_COPY;
			cfg->xsk->bind_flags |= XDP_ZEROCOPY;
			cfg->xsk->mode__zero_copy = true;
			log_info("DONE1");
			break;
		case 'p':
			cfg->xsk->mode__poll = true;
			break;
		case 'P':
			cfg->is_primary = true;
			cfg->total_sockets = atoi(optarg);
			log_info("DONE2");
			break;
		case 't':
			cfg->n_threads = atoi(optarg);
			log_info("DONE3");
			break;
		case 'b':
			cfg->xsk->mode__busy_poll = true;
			log_info("DONE4");
			break;
		case 'm':
			cfg->xsk->mode__need_wakeup = false;
			cfg->xsk->bind_flags &= ~XDP_USE_NEED_WAKEUP;
			break;
		case 'i':
			if (strlen(optarg) >= IF_NAMESIZE) {
				log_error(
					"ERROR: (Parsing error) --dev name too long\n");
				goto error;
			}
			char *ifname = (char *)&cfg->ifname;
			strncpy(ifname, optarg, IF_NAMESIZE);
			log_info("DONE5");
			break;
		case 'q':
			cfg->xsk->queue_mask = (__u32)strtol(optarg, NULL, 16);
			cfg->ifqueue = get_ifqueues(cfg->xsk->queue_mask,
						    cfg->n_threads);
			log_info("DONE6");
			break;
		case 'a':
			cfg->app_stats = true;
			break;
		case 'x':
			cfg->extra_stats = true;
			break;
		case 'I':
			cfg->irqs_at_init = -1;
			opt_irq_str = optarg;
			if (get_interrupt_number(cfg))
				cfg->irqs_at_init = get_irqs(cfg);
			if (cfg->irqs_at_init < 0) {
				log_error(
					"ERROR: (Parsing error) Failed to get irqs for %s\n",
					opt_irq_str);
				goto error;
			}
			break;
		case 'n':
			cfg->stats_interval = atoi(optarg);
			break;
		case 'Q':
			cfg->verbose = false;
			break;
		case 'w':
			if (get_clockid(&cfg->clock, optarg))
				log_error(
					"ERROR: Invalid clock %s. Default to CLOCK_MONOTONIC.\n",
					optarg);
			break;
		case 'F':
			cfg->frags_enabled = true;
			break;
		case 'h':
			full_help = true;
			/* fall-through */
error:
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
	cfg->umem = calloc(1, sizeof(struct xsk_umem_config));
	cfg->xsk = calloc(1, sizeof(struct xsk_config));
	if (!cfg->xsk || !cfg->umem) {
		log_error("ERROR: Memory allocation failed\n");
		exit(EXIT_FAILURE);
	}

	log_info("Parsing command line arguments1");
	cfg->is_primary = false;
	log_info("Parsing command line argumentsa");

	cfg->n_threads = 1;
	log_info("Parsing command line argumentsb");
	cfg->custom_xsk = false;
	cfg->reduce_cap = false;
	log_info("Parsing command line argumentsc");
	cfg->xsk->bind_flags = XDP_USE_NEED_WAKEUP;
	cfg->xsk->batch_size = BATCH_SIZE;
	cfg->xsk->mode__zero_copy = false;
	log_info("Parsing command line argumentsd");
	cfg->xsk->mode__busy_poll = false;
	log_info("Parsing command line argumentsf");

	cfg->xsk->mode__poll = false;
	log_info("Parsing command line argumentse");
	cfg->umem->frame_size = FRAME_SIZE;
	cfg->total_sockets = -1;
	cfg->stats_interval = 1;
	cfg->app_stats = false;
	cfg->extra_stats = false;
	cfg->frags_enabled = false;
	cfg->verbose = true;

	int ret = parse_cmdline_args(argc, argv, long_options, cfg, __doc__);

	log_info("Parsing command line arguments2");

	if ((cfg->umem->frame_size & (cfg->umem->frame_size - 1))) {
		log_error(
			"ERROR: (Parsing error) --frame-size=%d is not a power of two\n",
			cfg->umem->frame_size);
		exit(EXIT_FAILURE);
	}

	return ret;
}

int monitor__parse_cmdline_args(int argc, char **argv)
{
	argc++;
	argv++;
	int ret = 0;
	return ret;
}
