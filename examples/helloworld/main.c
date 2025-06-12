/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 *
 * helloworld: A simple helloworld NF that shows how to parse args for application 
 * and control plane setup using Flash monitor
 */

#include <signal.h>
#include <pthread.h>
#include <stdlib.h>

#include <flash_nf.h>
#include <flash_params.h>
#include <log.h>

struct config *cfg = NULL;
struct nf *nf = NULL;

// clang-format off
static const char *hw_options[] = {
    "-n <num>\tprint count",
    "-u \t\tprint hello universe",
    NULL
};
// clang-format on

struct appconf {
	int count;
	bool universe;
} app_conf;

static int parse_app_args(int argc, char **argv, struct appconf *app_conf, int shift)
{
	int c;
	opterr = 0;

	app_conf->count = 1;
	app_conf->universe = false;

	argc -= shift;
	argv += shift;

	while ((c = getopt(argc, argv, "hn:u")) != -1)
		switch (c) {
		case 'h':
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		case 'n':
			app_conf->count = atoi(optarg);
			break;
		case 'u':
			app_conf->universe = true;
			break;
		default:
			printf("Usage: %s -h\n", argv[-shift]);
			return -1;
		}

	return 0;
}

int main(int argc, char **argv)
{
	int shift;

	cfg = calloc(1, sizeof(struct config));
	if (!cfg) {
		log_error("ERROR: Memory allocation failed");
		exit(EXIT_FAILURE);
	}

	cfg->app_name = "Hello World Application";
	cfg->app_options = hw_options;

	shift = flash__parse_cmdline_args(argc, argv, cfg);
	if (shift < 0)
		goto out_cfg;

	if (parse_app_args(argc, argv, &app_conf, shift) < 0)
		goto out_cfg;

	if (flash__configure_nf(&nf, cfg) < 0)
		goto out_cfg;

	log_info("Control Plane setup done...");

	const char *message = app_conf.universe ? "Hello Universe!" : "Hello World!";
	for (int i = 0; i < app_conf.count; i++)
		log_info("%s", message);

	flash__xsk_close(cfg, nf);

	log_info("Control plane setup is working");

	return EXIT_SUCCESS;

out_cfg:
	free(cfg);
	exit(EXIT_FAILURE);
}
