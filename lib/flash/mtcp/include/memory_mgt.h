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

#ifndef MEMORY_MGT_H
#define MEMORY_MGT_H
/*----------------------------------------------------------------------------*/
#if !defined(DISABLE_DPDK) && !defined(ENABLE_ONVM)
#include <rte_common.h>
#include <rte_mempool.h>
/* for rte_versions retrieval */
#include <rte_version.h>
/*----------------------------------------------------------------------------*/
typedef struct rte_mempool mem_pool;
typedef struct rte_mempool *mem_pool_t;
/* create a memory pool with a chunk size and total size
   an return the pointer to the memory pool */
mem_pool_t MPCreate(char *name, int chunk_size, size_t total_size);
/*----------------------------------------------------------------------------*/
#else
struct mem_pool;
typedef struct mem_pool *mem_pool_t;

/* create a memory pool with a chunk size and total size
   an return the pointer to the memory pool */
mem_pool_t MPCreate(int chunk_size, size_t total_size);
#endif /* DISABLE_DPDK */
/*----------------------------------------------------------------------------*/
/* allocate one chunk */
void *MPAllocateChunk(mem_pool_t mp);

/* free one chunk */
void MPFreeChunk(mem_pool_t mp, void *p);

/* destroy the memory pool */
void MPDestroy(mem_pool_t mp);

/* retrun the number of free chunks */
int MPGetFreeChunks(mem_pool_t mp);
/*----------------------------------------------------------------------------*/
#endif /* MEMORY_MGT_H */
