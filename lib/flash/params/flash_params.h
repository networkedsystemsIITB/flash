/* SPDX-License-Identifier: Apache-2.0
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

/** 
 * Parse command line arguments for the flash application.
 * Allocates memory for umem and xsk configurations.
 * Sets default values for various parameters.
 *
 * @param argc Number of command line arguments
 * @param argv Array of command line arguments
 * @param cfg Pointer to the configuration structure to be filled
 * @return shift on success, -1 on failure; shift can be used to skip the parsed options
 */
int flash__parse_cmdline_args(int argc, char **argv, struct config *cfg);

#endif
