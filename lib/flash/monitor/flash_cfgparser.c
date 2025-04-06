/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <log.h>
#include <linux/if_link.h>
#include <cjson/cJSON.h>

#include "flash_monitor.h"

static void configure_nic(char *ifname, int total_queues, int mode)
{
	char total_queues_str[10];
	// Need to modify this path based on the system and NIC
	char script_path[256] = "./usertools/netdev/setup_mlx5.sh";
	char interface_flag[3] = "-i";
	char queues_flag[3] = "-q";
	char busy_poll_flag[3] = "-b";
	sprintf(total_queues_str, "%d", total_queues);

	if (fork() == 0) {
		if (freopen("/dev/null", "w", stdout) == NULL) {
			log_error("Error in freopen");
		}
		if (mode == FLASH__BUSY_POLL) {
			char *argv[] = { script_path, interface_flag, ifname, queues_flag, total_queues_str, busy_poll_flag, NULL };
			if (execvp(argv[0], argv)) {
				log_error("Error in execvp");
			}
			exit(0);
		} else {
			char *argv[] = { script_path, interface_flag, ifname, queues_flag, total_queues_str, NULL };
			if (execvp(argv[0], argv)) {
				log_error("Error in execvp");
			}
			exit(0);
		}
	} else {
		log_info("Waiting for NIC configuration to complete...");
		wait(NULL);
		log_info("NIC configuration completed");
	}
}
static __u32 get_flags(char *flag)
{
	if (strlen(flag) != 1) {
		log_error("Invalid xdp flag: %s", flag);
		exit(EXIT_FAILURE);
	}
	switch (flag[0]) {
	case 's':
		return XDP_FLAGS_SKB_MODE;
	case 'd':
		return XDP_FLAGS_DRV_MODE;
	case 'h':
		return XDP_FLAGS_HW_MODE;
	case 'c':
		return XDP_COPY;
	case 'z':
		return XDP_ZEROCOPY;
	case 'b':
		return FLASH__BUSY_POLL;
	case 'm':
		return FLASH__NO_NEED_WAKEUP;
	case 'p':
		return FLASH__POLL;
	}

	log_error("Invalid flag: %s", flag);
	exit(EXIT_FAILURE);
}

struct NFGroup *parse_json(const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		log_error("Unable to open file");
		return NULL;
	}

	fseek(file, 0, SEEK_END);
	long length = ftell(file);
	fseek(file, 0, SEEK_SET);
	char *json_data = (char *)calloc(1, length + 1);
	if (fread(json_data, 1, length, file) <= 0) {
		log_error("Error in reading json file");
		exit(EXIT_FAILURE);
	}
	json_data[length] = '\0';
	fclose(file);

	cJSON *root = cJSON_Parse(json_data);
	if (!root) {
		log_error("Error parsing JSON");
		return NULL;
	}

	free(json_data);

	int mode = 0, num_queues = 0;
	char ifname[IF_NAMESIZE + 1];
	// Extract "umem" array
	cJSON *umem_array = cJSON_GetObjectItem(root, "umem");
	if (!cJSON_IsArray(umem_array)) {
		log_error("Invalid or missing 'umem' array");
		cJSON_Delete(root);
		return NULL;
	}

	cJSON *route = cJSON_GetObjectItem(root, "route");

	// Allocate memory for NFGroup
	struct NFGroup *nf_group = (struct NFGroup *)calloc(1, sizeof(struct NFGroup));
	nf_group->umem_count = cJSON_GetArraySize(umem_array);
	nf_group->umem = (struct umem **)calloc(nf_group->umem_count, sizeof(struct umem *));

	// Iterate over each "umem" entry
	for (int i = 0; i < nf_group->umem_count; i++) {
		cJSON *umem_obj = cJSON_GetArrayItem(umem_array, i);
		nf_group->umem[i] = (struct umem *)calloc(1, sizeof(struct umem));
		nf_group->umem[i]->id = cJSON_GetObjectItem(umem_obj, "umem_id")->valueint;

		// ifname
		cJSON *ifname_obj = cJSON_GetObjectItem(umem_obj, "ifname");
		if (!cJSON_IsString(ifname_obj)) {
			log_error("Invalid or missing 'ifname'");
			cJSON_Delete(root);
			return NULL;
		}
		char *_ifname = strdup(ifname_obj->valuestring);

		nf_group->umem[i]->cfg = calloc(1, sizeof(struct config));
		strncpy(nf_group->umem[i]->cfg->ifname, _ifname, IF_NAMESIZE);
		log_info("ifname: %s", nf_group->umem[i]->cfg->ifname);
		nf_group->umem[i]->cfg->umem_id = nf_group->umem[i]->id;
		nf_group->umem[i]->cfg->nf_id = -1;
		nf_group->umem[i]->cfg->umem = calloc(1, sizeof(struct umem_config));
		nf_group->umem[i]->cfg->xsk = calloc(1, sizeof(struct xsk_config));

		cJSON *umem_scale = cJSON_GetObjectItem(umem_obj, "umem_scale");
		if (!cJSON_IsNumber(umem_scale)) {
			nf_group->umem[i]->cfg->umem_scale = 1;
		} else {
			nf_group->umem[i]->cfg->umem_scale = (uint16_t)umem_scale->valueint;
		}

		// Extract "xdp_flags"
		cJSON *xdp_flags_obj = cJSON_GetObjectItem(umem_obj, "xdp_flags");
		if (!cJSON_IsString(xdp_flags_obj)) {
			log_error("Invalid or missing 'xdp_flags'");
			cJSON_Delete(root);
			return NULL;
		}
		char *xdp_flags = strdup(xdp_flags_obj->valuestring);
		nf_group->umem[i]->cfg->xsk->xdp_flags = get_flags(xdp_flags);
		log_info("xdp_flags: %s", xdp_flags);

		// Extract "bind_flags"
		cJSON *bind_flags_obj = cJSON_GetObjectItem(umem_obj, "bind_flags");
		if (!cJSON_IsString(bind_flags_obj)) {
			log_error("Invalid or missing 'bind_flags'");
			cJSON_Delete(root);
			return NULL;
		}
		char *bind_flags = strdup(bind_flags_obj->valuestring);
		nf_group->umem[i]->cfg->xsk->bind_flags = get_flags(bind_flags);
		log_info("bind_flags: %s", bind_flags);

		if (xdp_flags[0] == 's' && bind_flags[0] == 'z') {
			log_error("Invalid combination of xdp_flags and bind_flags");
			cJSON_Delete(root);
			return NULL;
		}

		// Extract "mode"
		cJSON *mode_obj = cJSON_GetObjectItem(umem_obj, "mode");
		if (!cJSON_IsString(mode_obj)) {
			log_error("Invalid or missing 'mode'");
			cJSON_Delete(root);
			return NULL;
		}
		char *_mode = strdup(mode_obj->valuestring);
		if (strlen(_mode) == 0) {
			nf_group->umem[i]->cfg->xsk->bind_flags |= XDP_USE_NEED_WAKEUP;
		} else {
			nf_group->umem[i]->cfg->xsk->mode = get_flags(_mode);
		}
		log_info("mode: %s", _mode);

		// Extract "custom_xsk" array
		cJSON *custom_xsk_bool = cJSON_GetObjectItem(umem_obj, "custom_xsk");
		if (!cJSON_IsBool(custom_xsk_bool)) {
			log_error("Invalid or missing 'custom_xsk'");
			cJSON_Delete(root);
			return NULL;
		}
		nf_group->umem[i]->cfg->custom_xsk = custom_xsk_bool->valueint ? true : false;

		if (nf_group->umem[i]->cfg->custom_xsk) {
			log_warn("PLEASE MAKE SURE YOU LOADED CUSTOM XDP PROGRAM!");
		} else {
			log_info("USING DEFAULT XDP PROGRAM");
		}

		// Extract "frags_enabled" array
		cJSON *frags_enabled_bool = cJSON_GetObjectItem(umem_obj, "frags_enabled");
		if (!cJSON_IsBool(frags_enabled_bool)) {
			log_error("Invalid or missing 'frags_enabled'");
			cJSON_Delete(root);
			return NULL;
		}
		nf_group->umem[i]->cfg->frags_enabled = frags_enabled_bool->valueint ? true : false;

		// Extract "nf" array
		cJSON *nf_array = cJSON_GetObjectItem(umem_obj, "nf");
		if (!cJSON_IsArray(nf_array)) {
			log_error("Invalid or missing 'nf' array");
			cJSON_Delete(root);
			return NULL;
		}
		nf_group->umem[i]->nf_count = cJSON_GetArraySize(nf_array);
		nf_group->umem[i]->current_nf_count = 0;
		nf_group->umem[i]->nf = (struct nf **)calloc(nf_group->umem[i]->nf_count, sizeof(struct nf *));

		int total_threads = 0;

		// Iterate over each "nf" entry
		for (int j = 0; j < nf_group->umem[i]->nf_count; j++) {
			cJSON *nf_obj = cJSON_GetArrayItem(nf_array, j);
			nf_group->umem[i]->nf[j] = (struct nf *)calloc(1, sizeof(struct nf));
			nf_group->umem[i]->nf[j]->id = cJSON_GetObjectItem(nf_obj, "nf_id")->valueint;
			nf_group->umem[i]->nf[j]->is_up = false;
			cJSON *ip_obj = cJSON_GetObjectItem(nf_obj, "nf_ip");
			if (!cJSON_IsString(ip_obj)) {
				log_error("Invalid or missing 'nf_ip'");
				cJSON_Delete(root);
				return NULL;
			}
			strncpy(nf_group->umem[i]->nf[j]->ip, ip_obj->valuestring, INET_ADDRSTRLEN - 1);
			nf_group->umem[i]->nf[j]->ip[INET_ADDRSTRLEN - 1] = '\0'; // Ensure null-termination
			cJSON *port_obj = cJSON_GetObjectItem(nf_obj, "nf_port");
			if (!cJSON_IsNumber(port_obj)) {
				log_error("Invalid or missing 'nf_port'");
				cJSON_Delete(root);
				return NULL;
			}
			nf_group->umem[i]->nf[j]->port = (uint16_t)port_obj->valueint;

			log_info("nf_id: %d, nf_ip: %s, nf_port: %d", nf_group->umem[i]->nf[j]->id, nf_group->umem[i]->nf[j]->ip,
				 nf_group->umem[i]->nf[j]->port);

			char nf_id[5];
			sprintf(nf_id, "%d", nf_group->umem[i]->nf[j]->id);

			cJSON *route_item = cJSON_GetObjectItem(route, nf_id);

			if (!cJSON_IsArray(route_item)) {
				log_error("Invalid or missing 'route_item' array");
				cJSON_Delete(root);
				return NULL;
			}

			int edges = cJSON_GetArraySize(route_item);
			int *next = (int *)calloc(edges, sizeof(int) * edges);

			for (int l = 0; l < edges; l++) {
				cJSON *item = cJSON_GetArrayItem(route_item, l);
				if (item == NULL || !cJSON_IsNumber(item)) {
					log_error("Invalid array item at index %d", l);
					free(next);
					cJSON_Delete(root);
					return NULL;
				}
				next[l] = cJSON_GetNumberValue(item);
				log_info("%d -> %d", nf_group->umem[i]->nf[j]->id, next[l]);
			}

			cJSON *thread_array = cJSON_GetObjectItem(nf_obj, "thread");
			if (!cJSON_IsArray(thread_array)) {
				log_error("Invalid or missing 'thread' array");
				cJSON_Delete(root);
				return NULL;
			}
			nf_group->umem[i]->nf[j]->thread_count = cJSON_GetArraySize(thread_array);
			nf_group->umem[i]->nf[j]->current_thread_count = 0;
			nf_group->umem[i]->nf[j]->thread =
				(struct thread **)calloc(nf_group->umem[i]->nf[j]->thread_count, sizeof(struct thread *));

			// Iterate over each "thread" entry
			for (int k = 0; k < nf_group->umem[i]->nf[j]->thread_count; k++) {
				cJSON *thread_obj = cJSON_GetArrayItem(thread_array, k);
				nf_group->umem[i]->nf[j]->thread[k] = (struct thread *)calloc(1, sizeof(struct thread));
				nf_group->umem[i]->nf[j]->thread[k]->id = cJSON_GetObjectItem(thread_obj, "thread_id")->valueint;
				cJSON *queue_obj = cJSON_GetObjectItem(thread_obj, "queue");
				if (!cJSON_IsNumber(queue_obj)) {
					log_error("Invalid or missing 'queue'");
					cJSON_Delete(root);
					return NULL;
				}
				nf_group->umem[i]->nf[j]->thread[k]->ifqueue = (uint8_t)cJSON_GetNumberValue(queue_obj);
				log_info("thread_id: %d, queue: %d", nf_group->umem[i]->nf[j]->thread[k]->id,
					 nf_group->umem[i]->nf[j]->thread[k]->ifqueue);
				nf_group->umem[i]->nf[j]->thread[k]->socket = NULL;
				nf_group->umem[i]->nf[j]->thread[k]->xsk = NULL;
				nf_group->umem[i]->nf[j]->thread[k]->umem_offset = total_threads;
				total_threads++;
			}
			nf_group->umem[i]->nf[j]->next = next;
			nf_group->umem[i]->nf[j]->next_size = edges;
		}
		nf_group->umem[i]->cfg->total_sockets = total_threads;

		if (i == nf_group->umem_count - 1) {
			strncpy(ifname, _ifname, IF_NAMESIZE);
			mode = nf_group->umem[i]->cfg->xsk->mode;
		}

		num_queues += total_threads;
	}

	configure_nic(ifname, num_queues, mode);
	cJSON_Delete(root);

	return nf_group;
}

void free_nf_group(struct NFGroup *nf_group)
{
	if (nf_group == NULL) {
		return;
	}

	for (int i = 0; i < nf_group->umem_count; i++) {
		if (nf_group->umem[i] != NULL) {
			for (int j = 0; j < nf_group->umem[i]->nf_count; j++) {
				if (nf_group->umem[i]->nf[j] != NULL) {
					for (int k = 0; k < nf_group->umem[i]->nf[j]->thread_count; k++) {
						if (nf_group->umem[i]->nf[j]->thread[k] != NULL) {
							free(nf_group->umem[i]->nf[j]->thread[k]);
						}
					}
					free(nf_group->umem[i]->nf[j]->thread);
					free(nf_group->umem[i]->nf[j]->next);
					free(nf_group->umem[i]->nf[j]);
				}
			}
			free(nf_group->umem[i]->nf);
			free(nf_group->umem[i]->cfg->xsk);
			free(nf_group->umem[i]->cfg->umem);
			free(nf_group->umem[i]->cfg);
			free(nf_group->umem[i]);
		}
	}
	free(nf_group->umem);
	free(nf_group);
}
