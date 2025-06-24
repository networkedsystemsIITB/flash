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