/*
 * mTCP source code is distributed under the Modified BSD Licence.
 *
 * Copyright (C) 2015 EunYoung Jeong, Shinae Woo, Muhammad Jamshed, Haewon Jeong, 
 *                    Sunghwan Ihm, Dongsu Han, KyoungSoo Park
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <organization> nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

 #include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <sys/mman.h>
#include <unistd.h>
#include "debug.h"
#include "memory_mgt.h"
/*----------------------------------------------------------------------------*/
typedef struct tag_mem_chunk {
	int mc_free_chunks;
	struct tag_mem_chunk *mc_next;
} mem_chunk;
/*----------------------------------------------------------------------------*/
typedef mem_chunk *mem_chunk_t;
/*----------------------------------------------------------------------------*/
#if defined(DISABLE_DPDK) || defined(ENABLE_ONVM)
typedef struct mem_pool {
	u_char *mp_startptr;	/* start pointer */
	mem_chunk_t mp_freeptr; /* pointer to the start memory chunk */
	int mp_free_chunks;	/* number of total free chunks */
	int mp_total_chunks;	/* number of total free chunks */
	int mp_chunk_size;	/* chunk size in bytes */
	int mp_type;

} mem_pool;
/*----------------------------------------------------------------------------*/
mem_pool *MPCreate(int chunk_size, size_t total_size)
{
	mem_pool_t mp;

	if (chunk_size < (int)sizeof(mem_chunk)) {
		TRACE_ERROR("The chunk size should be larger than %lu. current: %d\n", sizeof(mem_chunk), chunk_size);
		return NULL;
	}
	if (chunk_size % 4 != 0) {
		TRACE_ERROR("The chunk size should be multiply of 4!\n");
		return NULL;
	}

	//assert(chunk_size <= 2*1024*1024);

	if ((mp = calloc(1, sizeof(mem_pool))) == NULL) {
		perror("calloc failed");
		exit(0);
	}
	mp->mp_type = 0;
	mp->mp_chunk_size = chunk_size;
	mp->mp_free_chunks = ((total_size + (chunk_size - 1)) / chunk_size);
	mp->mp_total_chunks = mp->mp_free_chunks;
	total_size = chunk_size * ((size_t)mp->mp_free_chunks);

	/* allocate the big memory chunk */
	int res = posix_memalign((void **)&mp->mp_startptr, getpagesize(), total_size);
	if (res != 0) {
		TRACE_ERROR("posix_memalign failed, size=%ld\n", total_size);
		assert(0);
		free(mp);
		return (NULL);
	}

	/* try mlock only for superuser */
	if (geteuid() == 0) {
		if (mlock(mp->mp_startptr, total_size) < 0)
			TRACE_ERROR("m_lock failed, size=%ld\n", total_size);
	}

	mp->mp_freeptr = (mem_chunk_t)mp->mp_startptr;
	mp->mp_freeptr->mc_free_chunks = mp->mp_free_chunks;
	mp->mp_freeptr->mc_next = NULL;

	return mp;
}
/*----------------------------------------------------------------------------*/
void *MPAllocateChunk(mem_pool_t mp)
{
	mem_chunk_t p = mp->mp_freeptr;

	if (mp->mp_free_chunks == 0)
		return (NULL);
	assert(p->mc_free_chunks > 0 && p->mc_free_chunks <= p->mc_free_chunks);

	p->mc_free_chunks--;
	mp->mp_free_chunks--;
	if (p->mc_free_chunks) {
		/* move right by one chunk */
		mp->mp_freeptr = (mem_chunk_t)((u_char *)p + mp->mp_chunk_size);
		mp->mp_freeptr->mc_free_chunks = p->mc_free_chunks;
		mp->mp_freeptr->mc_next = p->mc_next;
	} else {
		mp->mp_freeptr = p->mc_next;
	}

	return p;
}
/*----------------------------------------------------------------------------*/
void MPFreeChunk(mem_pool_t mp, void *p)
{
	mem_chunk_t mcp = (mem_chunk_t)p;

	//	assert((u_char*)p >= mp->mp_startptr &&
	//		   (u_char *)p < mp->mp_startptr + mp->mp_total_size);
	assert(((u_char *)p - mp->mp_startptr) % mp->mp_chunk_size == 0);
	//	assert(*((u_char *)p + (mp->mp_chunk_size-1)) == 'a');
	//	*((u_char *)p + (mp->mp_chunk_size-1)) = 'f';

	mcp->mc_free_chunks = 1;
	mcp->mc_next = mp->mp_freeptr;
	mp->mp_freeptr = mcp;
	mp->mp_free_chunks++;
}
/*----------------------------------------------------------------------------*/
void MPDestroy(mem_pool_t mp)
{
	free(mp->mp_startptr);
	free(mp);
}
/*----------------------------------------------------------------------------*/
int MPGetFreeChunks(mem_pool_t mp)
{
	return mp->mp_free_chunks;
}
/*----------------------------------------------------------------------------*/
// static uint32_t MPIsDanger(mem_pool_t mp)
// {
// #define DANGER_THRESHOLD 0.95
// #define SAFE_THRESHOLD 0.90
// 	uint32_t danger_num = mp->mp_total_chunks * DANGER_THRESHOLD;
// 	uint32_t safe_num = mp->mp_total_chunks * SAFE_THRESHOLD;
// 	if ((int)danger_num < mp->mp_total_chunks - mp->mp_free_chunks) {
// 		return mp->mp_total_chunks - mp->mp_free_chunks - safe_num;
// 	}
// 	return 0;
// }
// /*----------------------------------------------------------------------------*/
// static uint32_t MPIsOverSafeline(mem_pool_t mp)
// {
// #define SAFELINE 0.90
// 	uint32_t safe_num = mp->mp_total_chunks * SAFELINE;
// 	if ((int)safe_num < mp->mp_total_chunks - mp->mp_free_chunks) {
// 		return 1;
// 	}
// 	return 0;
// }
/*----------------------------------------------------------------------------*/
#else
/*----------------------------------------------------------------------------*/
mem_pool_t MPCreate(char *name, int chunk_size, size_t total_size)
{
	struct rte_mempool *mp;
	size_t sz, items;

	items = total_size / chunk_size;
	sz = RTE_ALIGN_CEIL(chunk_size, RTE_CACHE_LINE_SIZE);
	mp = rte_mempool_create(name, items, sz, 0, 0, NULL, 0, NULL, 0, rte_socket_id(), MEMPOOL_F_NO_SPREAD);

	if (mp == NULL) {
		TRACE_ERROR("Can't allocate memory for mempool!\n");
		exit(EXIT_FAILURE);
	}

	return mp;
}
/*----------------------------------------------------------------------------*/
void *MPAllocateChunk(mem_pool_t mp)
{
	int rc;
	void *buf;

	rc = rte_mempool_get(mp, (void **)&buf);
	if (rc != 0)
		return NULL;

	return buf;
}
/*----------------------------------------------------------------------------*/
void MPFreeChunk(mem_pool_t mp, void *p)
{
	rte_mempool_put(mp, p);
}
/*----------------------------------------------------------------------------*/
void MPDestroy(mem_pool_t mp)
{
#if RTE_VERSION < RTE_VERSION_NUM(16, 7, 0, 0)
	/* do nothing.. old versions don't have a method to reclaim back mem */
#else
	rte_mempool_free(mp);
#endif
}
/*----------------------------------------------------------------------------*/
int MPGetFreeChunks(mem_pool_t mp)
{
#if RTE_VERSION <= RTE_VERSION_NUM(16, 7, 0, 0)
	return (int)rte_mempool_free_count(mp);
#else
	return (int)rte_mempool_avail_count(mp);
#endif
}
/*----------------------------------------------------------------------------*/
#endif
