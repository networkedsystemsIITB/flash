/* SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2025 Debojeet Das
 */

#ifndef __FLASH_POOL_H
#define __FLASH_POOL_H

#include <flash_defines.h>

struct flash_pool {
	volatile uint32_t head;
	volatile uint32_t tail;
	volatile uint32_t size;
	volatile uint64_t desc[];
};

static inline bool flash_pool__get(struct flash_pool *pool, uint64_t *desc)
{
	if (!pool || pool->head == pool->tail)
		return false;

	*desc = pool->desc[pool->head++ & (pool->size - 1)];
	return true;
}

static inline bool flash_pool__put(struct flash_pool *pool, uint64_t desc)
{
	if (!pool || pool->tail - pool->head >= pool->size)
		return false;

	pool->desc[pool->tail++ & (pool->size - 1)] = desc;
	return true;
}

struct flash_pool *flash_pool__create(int frame_size, int umem_th_offset, int umem_scale);
void flash_pool__destroy(struct flash_pool *pool);

#endif /* __FLASH_POOL_H */