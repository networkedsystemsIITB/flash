/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <net/ethernet.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <log.h>

#include "flash_nf.h"

int flash__get_macaddr(struct config *cfg, struct ether_addr *addr)
{
	int fd;
	struct ifreq ifr;

	if (!cfg || !addr) {
		log_error("Invalid configuration or interface name");
		return -1;
	}

	fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
	if (fd < 0) {
		log_error("Unable to open socket for ioctl");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	log_info("Getting MAC address for interface: %s", cfg->ifname);

	strncpy(ifr.ifr_name, cfg->ifname, IF_NAMESIZE);
	ifr.ifr_name[IFNAMSIZ - 1] = '\0';

	if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1) {
		log_error("Unable to get MAC address");
		close(fd);
		return -1;
	}

	memcpy(addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

	close(fd);
	return 0;
}

void flash__hex_dump(void *pkt, size_t length, bool verbose)
{
	const unsigned char *address = (unsigned char *)pkt;
	const unsigned char *line = address;
	size_t line_size = 32;
	unsigned char c;
	char buf[32];
	int i = 0;

	if (verbose) {
		printf("length = %zu\n", length);
		printf("%s | ", buf);
	}

	while (length-- > 0) {
		printf("%02X ", *address++);
		if (!(++i % line_size) || (length == 0 && i % line_size)) {
			if (length == 0) {
				while (i++ % line_size)
					printf("__ ");
			}

			if (verbose) {
				printf(" | "); /* right close */
				while (line < address) {
					c = *line++;
					printf("%c", (c < 33 || c == 255) ? 0x2E : c);
				}
			}
			printf("\n");
			if (length > 0 && verbose)
				printf("%s | ", buf);
		}
	}
	printf("\n");
}