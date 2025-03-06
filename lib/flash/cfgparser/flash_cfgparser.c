#include <stdlib.h>
#include <stdlib.h>
#include <stdio.h>
#include <log.h>

#include "flash_cfgparser.h"

static int *get_ifqueues(__u32 ifqueue_mask, int total_sockets)
{
	log_info("QUEUE MASK: %d", ifqueue_mask);
	int *ifqueue = (int *)calloc(total_sockets, sizeof(int));
	int count = 0, thread = 0;
	while (ifqueue_mask != 0) {
		if ((ifqueue_mask & (1 << 0)) == 1)
			ifqueue[thread++] = count;
		ifqueue_mask >>= 1;
		count++;
	}
	return ifqueue;
}

static int count_ones(uint32_t mask)
{
	int count = 0;
	while (mask) {
		count += mask & 1;
		mask >>= 1;
	}
	return count;
}

struct NFGroup *parse_json(const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (file == NULL) {
		printf("Unable to open file\n");
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
		printf("Error parsing JSON\n");
		return NULL;
	}

	free(json_data);

	// Extract "umem" array
	cJSON *umem_array = cJSON_GetObjectItem(root, "umem");
	if (!cJSON_IsArray(umem_array)) {
		printf("Invalid or missing 'umem' array\n");
		cJSON_Delete(root);
		return NULL;
	}

	// Extract "custom_xsk" array
	cJSON *custom_xsk_bool = cJSON_GetObjectItem(root, "custom_xsk");
	if (!cJSON_IsBool(custom_xsk_bool)) {
		printf("Invalid or missing 'custom_xsk'\n");
		cJSON_Delete(root);
		return NULL;
	}
	bool custom_xsk = custom_xsk_bool->valueint ? true : false;

	if (custom_xsk) {
		log_error("PLEASE MAKE SURE YOU LOADED CUSTOM XDP PROGRAM!");
	} else {
		log_info("FINE");
	}

	// Allocate memory for NFGroup
	struct NFGroup *nf_group =
		(struct NFGroup *)calloc(1, sizeof(struct NFGroup));
	nf_group->umem_count = cJSON_GetArraySize(umem_array);
	nf_group->umem = (struct umem **)calloc(nf_group->umem_count,
						sizeof(struct umem *));

	// Iterate over each "umem" entry
	for (int i = 0; i < nf_group->umem_count; i++) {
		cJSON *umem_obj = cJSON_GetArrayItem(umem_array, i);
		nf_group->umem[i] =
			(struct umem *)calloc(1, sizeof(struct umem));
		nf_group->umem[i]->id =
			cJSON_GetObjectItem(umem_obj, "umem_id")->valueint;

		// ifname
		cJSON *ifname_obj = cJSON_GetObjectItem(umem_obj, "ifname");
		if (!cJSON_IsString(ifname_obj)) {
			printf("Invalid or missing 'ifname'\n");
			cJSON_Delete(root);
			return NULL;
		}
		char *ifname = strdup(ifname_obj->valuestring);

		// Extract "ifqueue_mask"
		cJSON *mask_obj = cJSON_GetObjectItem(umem_obj, "ifqueue_mask");
		if (!cJSON_IsString(mask_obj)) {
			printf("Invalid or missing 'ifqueue_mask'\n");
			cJSON_Delete(root);
			return NULL;
		}
		uint32_t ifqueue_mask =
			strtoul(mask_obj->valuestring, NULL, 16);

		// Extract "mode"
		cJSON *mode_obj = cJSON_GetObjectItem(umem_obj, "mode");
		if (!cJSON_IsString(mode_obj)) {
			printf("Invalid or missing 'mode'\n");
			cJSON_Delete(root);
			return NULL;
		}
		char *mode = strdup(mode_obj->valuestring);

		int queue_mask_ones = count_ones(ifqueue_mask);
		nf_group->umem[i]->cfg = calloc(1, sizeof(struct config));
		strncpy(nf_group->umem[i]->cfg->ifname, ifname, IF_NAMESIZE);
		nf_group->umem[i]->cfg->ifqueue =
			get_ifqueues(ifqueue_mask, queue_mask_ones);
		nf_group->umem[i]->cfg->total_sockets = queue_mask_ones;
		nf_group->umem[i]->cfg->custom_xsk = custom_xsk;
		nf_group->umem[i]->cfg->umem =
			calloc(1, sizeof(struct umem_config));
		nf_group->umem[i]->cfg->xsk =
			calloc(1, sizeof(struct xsk_config));
		if (strcmp(mode, "b") == 0) {
			log_info("ENABLING BUSY POLL");
			nf_group->umem[i]->cfg->xsk->mode__busy_poll = true;
			nf_group->umem[i]->cfg->xsk->mode__zero_copy = true;
		} else if (strcmp(mode, "p") == 0) {
			nf_group->umem[i]->cfg->xsk->mode__poll = true;
			cJSON *timeout_obj =
				cJSON_GetObjectItem(umem_obj, "timeout");
			if (!cJSON_IsString(timeout_obj)) {
				printf("Invalid or missing 'timeout'\n");
				cJSON_Delete(root);
				return NULL;
			}
			int timeout = atoi(timeout_obj->valuestring);
			nf_group->umem[i]->cfg->xsk->poll_timeout = timeout;
		} else if (strcmp(mode, "m") == 0) {
			nf_group->umem[i]->cfg->xsk->mode__need_wakeup = false;
		} else {
			log_error("Invalid mode");
		}

		// Extract "nf" array
		cJSON *nf_array = cJSON_GetObjectItem(umem_obj, "nf");
		if (!cJSON_IsArray(nf_array)) {
			printf("Invalid or missing 'nf' array\n");
			cJSON_Delete(root);
			return NULL;
		}
		nf_group->umem[i]->nf_count = cJSON_GetArraySize(nf_array);
		nf_group->umem[i]->current_nf_count = 0;
		nf_group->umem[i]->nf = (struct nf **)calloc(
			nf_group->umem[i]->nf_count, sizeof(struct nf *));

		int total_threads = 0;

		// Iterate over each "nf" entry
		for (int j = 0; j < nf_group->umem[i]->nf_count; j++) {
			cJSON *nf_obj = cJSON_GetArrayItem(nf_array, j);
			nf_group->umem[i]->nf[j] =
				(struct nf *)calloc(1, sizeof(struct nf));
			nf_group->umem[i]->nf[j]->id =
				cJSON_GetObjectItem(nf_obj, "nf_id")->valueint;
			nf_group->umem[i]->nf[j]->is_up = false;

			// Extract "thread" array
			cJSON *thread_array =
				cJSON_GetObjectItem(nf_obj, "thread");
			if (!cJSON_IsArray(thread_array)) {
				printf("Invalid or missing 'thread' array\n");
				cJSON_Delete(root);
				return NULL;
			}
			nf_group->umem[i]->nf[j]->thread_count =
				cJSON_GetArraySize(thread_array);
			nf_group->umem[i]->nf[j]->current_thread_count = 0;
			nf_group->umem[i]->nf[j]->thread =
				(struct thread **)calloc(
					nf_group->umem[i]->nf[j]->thread_count,
					sizeof(struct thread *));

			// Iterate over each "thread" entry
			for (int k = 0;
			     k < nf_group->umem[i]->nf[j]->thread_count; k++) {
				cJSON *thread_obj =
					cJSON_GetArrayItem(thread_array, k);
				nf_group->umem[i]->nf[j]->thread[k] =
					(struct thread *)calloc(
						1, sizeof(struct thread));
				nf_group->umem[i]->nf[j]->thread[k]->id =
					cJSON_GetObjectItem(thread_obj,
							    "thread_id")
						->valueint;
				nf_group->umem[i]->nf[j]->thread[k]->socket =
					NULL;
				nf_group->umem[i]->nf[j]->thread[k]->xsk = NULL;
				nf_group->umem[i]->nf[j]->thread[k]->umem_offset =
					total_threads;
				total_threads++;
			}
		}

		// Validate ifqueue_mask against total threads
		if (queue_mask_ones != total_threads) {
			printf("Error: ifqueue_mask has %d active queues, but there are %d total threads!\n",
			       queue_mask_ones, total_threads);
			cJSON_Delete(root);
			return NULL;
		}
	}

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
					for (int k = 0;
					     k < nf_group->umem[i]
							 ->nf[j]
							 ->thread_count;
					     k++) {
						if (nf_group->umem[i]
							    ->nf[j]
							    ->thread[k] !=
						    NULL) {
							free(nf_group->umem[i]
								     ->nf[j]
								     ->thread[k]);
						}
					}
					free(nf_group->umem[i]->nf[j]->thread);
					free(nf_group->umem[i]->nf[j]);
				}
			}
			free(nf_group->umem[i]->nf);
			free(nf_group->umem[i]->cfg->xsk);
			free(nf_group->umem[i]->cfg->umem);
			free(nf_group->umem[i]->cfg->ifqueue);
			free(nf_group->umem[i]->cfg);
			free(nf_group->umem[i]);
		}
	}
	free(nf_group->umem);
	free(nf_group);
}
