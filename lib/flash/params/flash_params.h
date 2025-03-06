/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2025 Debojeet Das
 */
#ifndef __FLASH_PARAMS_H
#define __FLASH_PARAMS_H

#include <getopt.h>

#define BUFSIZE 30

struct option_wrapper {
	struct option option;
	const char *help;
	const char *metavar;
	bool required;
};

int flash__parse_cmdline_args(int argc, char **argv, struct config *cfg);
int get_irqs(struct config *cfg);

#endif