/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */
#include <stdlib.h>

#include <log.h>

#include "flash_pool.h"

struct flash_pool *flash_pool__create(int frame_size, int umem_th_offset, int umem_scale)
{
	if (umem_th_offset < 0 || umem_scale <= 0) {
		log_error("Invalid parameters for flash_pool__create");
		return NULL;
	}

	uint32_t nr_frames = (size_t)XSK_RING_PROD__DEFAULT_NUM_DESCS * (size_t)2 * (size_t)umem_scale;

	struct flash_pool *pool = (struct flash_pool *)malloc(sizeof(struct flash_pool) + nr_frames * sizeof(uint64_t));
	if (!pool) {
		log_error("Memory allocation failed for flash_pool");
		return NULL;
	}

	pool->head = 0;
	pool->tail = 0;
	pool->size = nr_frames;

	for (uint32_t i = umem_th_offset * nr_frames; i < nr_frames * (umem_th_offset + 1); i++)
		pool->desc[pool->tail++] = i * frame_size;

	return pool;
}

void flash_pool__destroy(struct flash_pool *pool)
{
	if (pool)
		free(pool);
}
