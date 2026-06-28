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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>

#include "tcp_ring_buffer.h"
#include "tcp_rb_frag_queue.h"
#include "memory_mgt.h"
#include "debug.h"

#define MAX_RB_SIZE (16 * 1024 * 1024)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#ifdef ENABLELRO
#define __MEMCPY_DATA_2_BUFFER                                                            \
	mtcp_manager_t mtcp = rbm->mtcp;                                                  \
	if (mtcp->iom == &dpdk_module_func && len > TCP_DEFAULT_MSS)                      \
		mtcp->iom->dev_ioctl(mtcp->ctx, 0, PKT_RX_TCP_LROSEG, buff->head + putx); \
	else                                                                              \
		memcpy(buff->head + putx, data, len);
#endif
/*----------------------------------------------------------------------------*/
struct rb_manager {
	size_t chunk_size;
	uint32_t cur_num;
	uint32_t cnum;

	mem_pool_t mp;
	mem_pool_t frag_mp;

	rb_frag_queue_t free_fragq;	/* free fragment queue (for app thread) */
	rb_frag_queue_t free_fragq_int; /* free fragment quuee (only for mtcp) */

#ifdef MTCP_RX_ZERO_COPY
	mem_pool_t frag_mp_zc;
    rb_frag_queue_zc_t free_fragq_zc; // app thread
    rb_frag_queue_zc_t free_fragq_zc_int; // mtcp thread
#endif /* MTCP_RX_ZERO_COPY */

#ifdef ENABLELRO
	mtcp_manager_t mtcp;
#endif
} rb_manager;
/*----------------------------------------------------------------------------*/
uint32_t RBGetCurnum(rb_manager_t rbm)
{
	return rbm->cur_num;
}
/*-----------------------------------------------------------------------------*/
void RBPrintInfo(struct tcp_ring_buffer *buff)
{
	printf("buff_data %p, buff_size %d, buff_mlen %d, "
	       "buff_clen %lu, buff_head %p (%d), buff_tail (%d)\n",
	       buff->data, buff->size, buff->merged_len, buff->cum_len, buff->head, buff->head_offset, buff->tail_offset);
}
/*----------------------------------------------------------------------------*/
void RBPrintStr(struct tcp_ring_buffer *buff)
{
	RBPrintInfo(buff);
	printf("%s\n", buff->head);
}
/*----------------------------------------------------------------------------*/
void RBPrintHex(struct tcp_ring_buffer *buff)
{
	int i;

	RBPrintInfo(buff);

	for (i = 0; i < buff->merged_len; i++) {
		if (i != 0 && i % 16 == 0)
			printf("\n");
		printf("%0x ", *((unsigned char *)buff->head + i));
	}
	printf("\n");
}
/*----------------------------------------------------------------------------*/
rb_manager_t RBManagerCreate(mtcp_manager_t mtcp, size_t chunk_size, uint32_t cnum)
{
	(void)mtcp;
	rb_manager_t rbm = (rb_manager_t)calloc(1, sizeof(rb_manager));

	if (!rbm) {
		perror("rbm_create calloc");
		return NULL;
	}

	rbm->chunk_size = chunk_size;
	rbm->cnum = cnum;
#if !defined(DISABLE_DPDK) && !defined(ENABLE_ONVM)
	char pool_name[RTE_MEMPOOL_NAMESIZE];
	sprintf(pool_name, "rbm_pool_%u", mtcp->ctx->cpu);
	rbm->mp = (mem_pool_t)MPCreate(pool_name, chunk_size, (uint64_t)chunk_size * cnum);
#else
	rbm->mp = (mem_pool_t)MPCreate(chunk_size, (uint64_t)chunk_size * cnum);
#endif
	if (!rbm->mp) {
		TRACE_ERROR("Failed to allocate mp pool.\n");
		free(rbm);
		return NULL;
	}
#if !defined(DISABLE_DPDK) && !defined(ENABLE_ONVM)
	sprintf(pool_name, "frag_mp_%u", mtcp->ctx->cpu);
	rbm->frag_mp = (mem_pool_t)MPCreate(pool_name, sizeof(struct fragment_ctx), sizeof(struct fragment_ctx) * cnum);
#else
	rbm->frag_mp = (mem_pool_t)MPCreate(sizeof(struct fragment_ctx), sizeof(struct fragment_ctx) * cnum);
#endif
	if (!rbm->frag_mp) {
		TRACE_ERROR("Failed to allocate frag_mp pool.\n");
		MPDestroy(rbm->mp);
		free(rbm);
		return NULL;
	}

	rbm->free_fragq = CreateRBFragQueue(cnum);
	if (!rbm->free_fragq) {
		TRACE_ERROR("Failed to create free fragment queue.\n");
		MPDestroy(rbm->mp);
		MPDestroy(rbm->frag_mp);
		free(rbm);
		return NULL;
	}
	rbm->free_fragq_int = CreateRBFragQueue(cnum);
	if (!rbm->free_fragq_int) {
		TRACE_ERROR("Failed to create internal free fragment queue.\n");
		MPDestroy(rbm->mp);
		MPDestroy(rbm->frag_mp);
		DestroyRBFragQueue(rbm->free_fragq);
		free(rbm);
		return NULL;
	}

#ifdef MTCP_RX_ZERO_COPY
	rbm->frag_mp_zc = (mem_pool_t)MPCreate(sizeof(struct fragment_ctx_zc), sizeof(struct fragment_ctx_zc) * cnum);
	if (!rbm->frag_mp_zc) {
		TRACE_ERROR("Failed to allocate frag_mp_zc pool.\n");
		MPDestroy(rbm->mp);
		MPDestroy(rbm->frag_mp);
		DestroyRBFragQueue(rbm->free_fragq);
		DestroyRBFragQueue(rbm->free_fragq_int);
		free(rbm);
		return NULL;
	}


	rbm->free_fragq_zc = CreateRBFragQueue_zc(cnum);
	if (!rbm->free_fragq_zc) {
		TRACE_ERROR("Failed to create zero-copy free fragment queue.\n");
		MPDestroy(rbm->mp);
		MPDestroy(rbm->frag_mp);
		MPDestroy(rbm->frag_mp_zc);
		DestroyRBFragQueue(rbm->free_fragq);
		DestroyRBFragQueue(rbm->free_fragq_int);
		free(rbm);
		return NULL;
	}

	rbm->free_fragq_zc_int = CreateRBFragQueue_zc(cnum);
	if (!rbm->free_fragq_zc_int) {
		TRACE_ERROR("Failed to create internal zero-copy free fragment queue.\n");
		MPDestroy(rbm->mp);
		MPDestroy(rbm->frag_mp);
		MPDestroy(rbm->frag_mp_zc);
		DestroyRBFragQueue(rbm->free_fragq);
		DestroyRBFragQueue(rbm->free_fragq_int);
		DestroyRBFragQueue_zc(rbm->free_fragq_zc);
		free(rbm);
		return NULL;
	}
#endif /* MTCP_RX_ZERO_COPY */

#ifdef ENABLELRO
	rbm->mtcp = mtcp;
#endif
	return rbm;
}
/*----------------------------------------------------------------------------*/
static inline void FreeFragmentContextSingle(rb_manager_t rbm, struct fragment_ctx *frag)
{
	if (frag->is_calloc)
		free(frag);
	else
		MPFreeChunk(rbm->frag_mp, frag);
}
/*----------------------------------------------------------------------------*/
static void FreeFragmentContext(rb_manager_t rbm, struct fragment_ctx *fctx)
{
	struct fragment_ctx *remove;

	assert(fctx);
	if (fctx == NULL)
		return;

	while (fctx) {
		remove = fctx;
		fctx = fctx->next;
		FreeFragmentContextSingle(rbm, remove);
	}
}
/*----------------------------------------------------------------------------*/
#ifdef MTCP_RX_ZERO_COPY
static void
FreeFragmentContext_zc(rb_manager_t rbm,
                      struct fragment_ctx_zc *frag,
                      int option)
{
    if (option == AT_APP)
        RBFragEnqueue_zc(rbm->free_fragq_zc, frag);
    else
        RBFragEnqueue_zc(rbm->free_fragq_zc_int, frag);
}
#endif /* MTCP_RX_ZERO_COPY */
/*----------------------------------------------------------------------------*/
static struct fragment_ctx *AllocateFragmentContext(rb_manager_t rbm)
{
	/* this function should be called only in mtcp thread */
	struct fragment_ctx *frag;

	/* first try deqeue the fragment in free fragment queue */
	frag = RBFragDequeue(rbm->free_fragq);
	if (!frag) {
		frag = RBFragDequeue(rbm->free_fragq_int);
		if (!frag) {
			/* next fall back to fetching from mempool */
			frag = MPAllocateChunk(rbm->frag_mp);
			if (!frag) {
				TRACE_ERROR("fragments depleted, fall back to calloc\n");
				frag = calloc(1, sizeof(struct fragment_ctx));
				if (frag == NULL) {
					TRACE_ERROR("calloc failed\n");
					exit(-1);
				}
				frag->is_calloc = 1; /* mark it as allocated by calloc */
			}
		}
	}
	memset(frag, 0, sizeof(*frag));
	return frag;
}
/*----------------------------------------------------------------------------*/
struct tcp_ring_buffer *RBInit(rb_manager_t rbm, uint32_t init_seq)
{
	struct tcp_ring_buffer *buff = (struct tcp_ring_buffer *)calloc(1, sizeof(struct tcp_ring_buffer));

	if (buff == NULL) {
		perror("rb_init buff");
		return NULL;
	}

	buff->data = MPAllocateChunk(rbm->mp);
	if (!buff->data) {
		perror("rb_init MPAllocateChunk");
		free(buff);
		return NULL;
	}

	//memset(buff->data, 0, rbm->chunk_size);

	buff->size = rbm->chunk_size;
	buff->head = buff->data;
	buff->head_seq = init_seq;
	buff->init_seq = init_seq;

	rbm->cur_num++;

	return buff;
}
/*----------------------------------------------------------------------------*/
void RBFree(mtcp_manager_t mtcp, rb_manager_t rbm, struct tcp_ring_buffer *buff)
{
	UNUSED(mtcp);
	assert(buff);
	if (buff->fctx) {
		FreeFragmentContext(rbm, buff->fctx);
		buff->fctx = NULL;
	}

	if (buff->data) {
		MPFreeChunk(rbm->mp, buff->data);
	}

	rbm->cur_num--;

#ifdef MTCP_RX_ZERO_COPY
	while (buff->fctx_zc) {
 	   struct fragment_ctx_zc *tmp = buff->fctx_zc;
	    buff->fctx_zc = tmp->next;
		mtcp->iom->release_pkt(mtcp->ctx, -2, (uint8_t *)tmp->flash_addr, tmp->len);
	    FreeFragmentContext_zc(rbm, tmp, AT_MTCP); // since RBFree is only called by mtcp thread only
	}
#endif /* MTCP_RX_ZERO_COPY */


	free(buff);
}
/*----------------------------------------------------------------------------*/
#define MAXSEQ ((uint32_t)(0xFFFFFFFF))
/*----------------------------------------------------------------------------*/
static inline uint32_t GetMinSeq(uint32_t a, uint32_t b)
{
	if (a == b)
		return a;
	if (a < b)
		return ((b - a) <= MAXSEQ / 2) ? a : b;
	/* b < a */
	return ((a - b) <= MAXSEQ / 2) ? b : a;
}
/*----------------------------------------------------------------------------*/
static inline uint32_t GetMaxSeq(uint32_t a, uint32_t b)
{
	if (a == b)
		return a;
	if (a < b)
		return ((b - a) <= MAXSEQ / 2) ? b : a;
	/* b < a */
	return ((a - b) <= MAXSEQ / 2) ? a : b;
}
/*----------------------------------------------------------------------------*/
static inline int CanMerge(const struct fragment_ctx *a, const struct fragment_ctx *b)
{
	uint32_t a_end = a->seq + a->len + 1;
	uint32_t b_end = b->seq + b->len + 1;

	if (GetMinSeq(a_end, b->seq) == a_end || GetMinSeq(b_end, a->seq) == b_end)
		return 0;
	return (1);
}
/*----------------------------------------------------------------------------*/
static inline void MergeFragments(struct fragment_ctx *a, struct fragment_ctx *b)
{
	/* merge a into b */
	uint32_t min_seq, max_seq;

	min_seq = GetMinSeq(a->seq, b->seq);
	max_seq = GetMaxSeq(a->seq + a->len, b->seq + b->len);
	b->seq = min_seq;
	b->len = max_seq - min_seq;
}
/*----------------------------------------------------------------------------*/
int RBPut(rb_manager_t rbm, struct tcp_ring_buffer *buff, void *data, uint32_t len, uint32_t cur_seq)
{
	int putx, end_off;
	struct fragment_ctx *new_ctx;
	struct fragment_ctx *iter;
	struct fragment_ctx *prev, *pprev;
	int merged = 0;

	if (len <= 0)
		return 0;

	// if data offset is smaller than head sequence, then drop
	if (GetMinSeq(buff->head_seq, cur_seq) != buff->head_seq)
		return 0;

	putx = cur_seq - buff->head_seq;
	end_off = putx + len;
	if (buff->size < end_off) {
		return -2;
	}

	// if buffer is at tail, move the data to the first of head
	if (buff->size <= ((int)buff->head_offset + end_off)) {
		memmove(buff->data, buff->head, buff->last_len);
		buff->tail_offset -= buff->head_offset;
		buff->head_offset = 0;
		buff->head = buff->data;
	}
#ifdef ENABLELRO
	// copy data to buffer
	__MEMCPY_DATA_2_BUFFER;
#else
	//copy data to buffer
	memcpy(buff->head + putx, data, len);
#endif
	if (buff->tail_offset < buff->head_offset + end_off)
		buff->tail_offset = buff->head_offset + end_off;
	buff->last_len = buff->tail_offset - buff->head_offset;

	// create fragmentation context blocks
	new_ctx = AllocateFragmentContext(rbm);
	if (!new_ctx) {
		perror("allocating new_ctx failed");
		return 0;
	}
	new_ctx->seq = cur_seq;
	new_ctx->len = len;
	new_ctx->next = NULL;

	// traverse the fragment list, and merge the new fragment if possible
	for (iter = buff->fctx, prev = NULL, pprev = NULL; iter != NULL; pprev = prev, prev = iter, iter = iter->next) {
		if (CanMerge(new_ctx, iter)) {
			/* merge the first fragment into the second fragment */
			MergeFragments(new_ctx, iter);

			/* remove the first fragment */
			if (prev == new_ctx) {
				if (pprev)
					pprev->next = iter;
				else
					buff->fctx = iter;
				prev = pprev;
			}
			FreeFragmentContextSingle(rbm, new_ctx);
			new_ctx = iter;
			merged = 1;
		} else if (merged || GetMaxSeq(cur_seq + len, iter->seq) == iter->seq) {
			/* merged at some point, but no more mergeable
			   then stop it now */
			break;
		}
	}

	if (!merged) {
		if (buff->fctx == NULL) {
			buff->fctx = new_ctx;
		} else if (GetMinSeq(cur_seq, buff->fctx->seq) == cur_seq) {
			/* if the new packet's seqnum is before the existing fragments */
			new_ctx->next = buff->fctx;
			buff->fctx = new_ctx;
		} else {
			/* if the seqnum is in-between the fragments or
			   at the last */
			assert(GetMinSeq(cur_seq, prev->seq + prev->len) == prev->seq + prev->len);
			prev->next = new_ctx;
			new_ctx->next = iter;
		}
	}
	if (buff->head_seq == buff->fctx->seq) {
		buff->cum_len += buff->fctx->len - buff->merged_len;
		buff->merged_len = buff->fctx->len;
	}

	return len;
}
/*----------------------------------------------------------------------------*/
size_t RBRemove(rb_manager_t rbm, struct tcp_ring_buffer *buff, size_t len, int option)
{
	/* this function should be called only in application thread */

	if (buff->merged_len < (int)len)
		len = buff->merged_len;

	if (len == 0)
		return 0;

	buff->head_offset += len;
	buff->head = buff->data + buff->head_offset;
	buff->head_seq += len;

	buff->merged_len -= len;
	buff->last_len -= len;

	// modify fragementation chunks
	if (len == buff->fctx->len) {
		struct fragment_ctx *remove = buff->fctx;
		buff->fctx = buff->fctx->next;
		if (option == AT_APP) {
			RBFragEnqueue(rbm->free_fragq, remove);
		} else if (option == AT_MTCP) {
			RBFragEnqueue(rbm->free_fragq_int, remove);
		}
	} else if (len < buff->fctx->len) {
		buff->fctx->seq += len;
		buff->fctx->len -= len;
	} else {
		assert(0);
	}

	return len;
}
/*----------------------------------------------------------------------------*/
#ifdef MTCP_RX_ZERO_COPY
static struct fragment_ctx_zc *
AllocateFragmentContext_zc(rb_manager_t rbm)
{
    struct fragment_ctx_zc *frag;

    frag = RBFragDequeue_zc(rbm->free_fragq_zc);
    if (!frag) {
        frag = RBFragDequeue_zc(rbm->free_fragq_zc_int);
        if (!frag) {
            frag = MPAllocateChunk(rbm->frag_mp_zc);
            if (!frag) {
                frag = calloc(1, sizeof(*frag));
                if (!frag)
                    exit(-1);
            }
        }
    }

    memset(frag, 0, sizeof(*frag));
    return frag;
}

size_t RBAvailable_zc(struct tcp_ring_buffer *buff)
{
    return buff->merged_len;
}

int RBPut_zc(mtcp_manager_t mtcp, rb_manager_t rbm, struct tcp_ring_buffer *buff,
             uint64_t flash_addr,
             unsigned char *payload,
             uint32_t len,
             uint32_t cur_seq)
{
	struct fragment_ctx_zc *new_ctx;
    uint32_t expect = buff->head_seq + buff->merged_len;
    uint32_t end_seq = cur_seq + len;

	// 1. reject packets that wont contribute to merged_len
	// h-> it is okay to reject out of order packets, not much drop in performance
	// mtcp immediately sends an ack out, telling the sender to retransmit the missing packet
	if (len == 0 || 
        GetMinSeq(end_seq, expect) == end_seq ||  // end_seq <= expect
        GetMinSeq(expect, cur_seq) != cur_seq) {  // expect < cur_seq
        mtcp->iom->release_pkt(mtcp->ctx, -1, (uint8_t *)flash_addr, len);
        return 0;
    }

	// h-> at this point, cur_seq <= expect and expect < end_seq

	// overlap adjustment
	// h-> i am making sure that the fragments do not overlap in data,
	// therefore i am adjusting the payload and cur_seq to ensure that the new fragment starts exactly at expect
	uint32_t offset = expect - cur_seq;
	if (offset >= len) {
		mtcp->iom->release_pkt(mtcp->ctx, -1, (uint8_t *)flash_addr, len);
		return 0;
	}
	payload += offset;
	cur_seq = expect;
	len -= offset;
	end_seq = cur_seq + len;

	new_ctx = AllocateFragmentContext_zc(rbm);
    if (!new_ctx) {
		mtcp->iom->release_pkt(mtcp->ctx, -1, (uint8_t *)flash_addr, len);
        return 0;
	}
    new_ctx->seq = cur_seq;
    new_ctx->len = len;
    new_ctx->payload = payload;
    new_ctx->payload_len = len;
    new_ctx->flash_addr = flash_addr;
    new_ctx->next = NULL;

    // h-> now we can simply append this pkt at the tail of the list
	if (!buff->fctx_zc) {
		buff->fctx_zc = new_ctx;
		buff->fctx_zc_tail = new_ctx;
	} else {
		buff->fctx_zc_tail->next = new_ctx;
		buff->fctx_zc_tail = new_ctx;
	}

	// update merged_len
	buff->merged_len += len;
    return len;
}

size_t RBRead_zc(rb_manager_t rbm, struct tcp_ring_buffer *buff,
                 void *dst, size_t len)
{
    struct fragment_ctx_zc *f;
    size_t copied = 0;
    unsigned char *out = dst;

    (void)rbm;

    if ((size_t)buff->merged_len < len)
        len = (size_t)buff->merged_len;

    if (len == 0)
        return 0;

    f = buff->fctx_zc;

    // h-> RBPut_zc guarantees:
    // 1. fctx_zc starts exactly at head_seq
    // 2. Each fragment's seq is exactly the previous fragment's end_seq
    // 3. There are no overlaps
    // Therefore we can simply iterate and copy.

    while (f && copied < len) {
        uint32_t remaining_to_copy = len - copied;
        uint32_t can_copy_from_this_frag = f->payload_len;

        if (can_copy_from_this_frag > remaining_to_copy) {
            can_copy_from_this_frag = remaining_to_copy;
        }

		// h-> fragments point to non-overlapping payloads, so we can safely copy
        memcpy(out + copied, f->payload, can_copy_from_this_frag);

        copied += can_copy_from_this_frag;
        f = f->next;
    }

    return copied;
}

size_t RBRemove_zc(mtcp_manager_t mtcp, rb_manager_t rbm, struct tcp_ring_buffer *buff,
                   size_t len, int option)
{
    /* this function should be called only in application thread */
    (void)rbm;

    if ((size_t)buff->merged_len < len)
        len = buff->merged_len;

    if (len == 0)
        return 0;

    struct fragment_ctx_zc *f;
    size_t to_remove = len;

    while (to_remove > 0 && buff->fctx_zc) {
        f = buff->fctx_zc;

        if (to_remove < f->payload_len) {
             // Partial removal: 
             // We don't free the fragment yet, just move the logical start.
             // This maintains the invariant: f->seq remains equal to head_seq.
            f->seq         += to_remove;
            f->payload     += to_remove;
            f->payload_len -= to_remove;

            buff->head_seq += to_remove;
            to_remove = 0; 
        } else {
            // Full removal
            buff->fctx_zc = f->next;

            if (buff->fctx_zc == NULL) {
                buff->fctx_zc_tail = NULL;
            }

            uint32_t consumed = f->payload_len;
            
            // Release the actual packet buffer and metadata
			if (option == AT_APP) {
				RBFreeBuff_zc(mtcp, rbm, f->flash_addr);
			} else {
				mtcp->iom->release_pkt(mtcp->ctx, -2, (uint8_t *)f->flash_addr, f->len);
			}
            FreeFragmentContext_zc(rbm, f, option);

            buff->head_seq += consumed;
            to_remove -= consumed;
        }
    }

    // h-> since everything was already contiguous
    buff->merged_len -= len;

    return len;
}

size_t RBGetBuff_zc(rb_manager_t rbm, struct tcp_ring_buffer *buff, void **buf, uint64_t* flash_addr)
{
	if (!buff) {
		*buf = NULL;
		*flash_addr = 0;
		return 0;
	}

	/* this function should be called only in application thread */
    (void)rbm;

    if (!buff->fctx_zc) {
		*buf = NULL;
		*flash_addr = 0;
        return 0;
	}

    struct fragment_ctx_zc *f = buff->fctx_zc;
	buff->fctx_zc = f->next;
	if (!buff->fctx_zc) {
		buff->fctx_zc_tail = NULL;
	}

	*buf = f->payload;
	*flash_addr = f->flash_addr;
	buff->head_seq += f->payload_len;
	buff->merged_len -= f->payload_len;

	FreeFragmentContext_zc(rbm, f, AT_APP);

	return f->payload_len;
}

// app thread produces
// mtcp thread consumes
void RBFreeBuff_zc(mtcp_manager_t mtcp, rb_manager_t rbm, uint64_t flash_addr)
{
	UNUSED(mtcp);
	UNUSED(rbm);

	struct zc_rx_free_ring *ring = &mtcp->ctx->flash_ctx.zc_rx_ring;

	uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
	uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);
    
    // overflow
    if (tail - head >= ZC_RX_FREE_RING_SIZE) {
		printf("not possible: ring size was taken too big\n");
		errno = EAGAIN;
        return;
    }

    ring->flash_addrs[tail & (ZC_RX_FREE_RING_SIZE - 1)] = flash_addr;
    
	__atomic_store_n(&ring->tail, tail + 1, __ATOMIC_RELEASE);
}

int RBFreeBuff_zc_batch(mtcp_manager_t mtcp, rb_manager_t rbm, uint64_t *addr_array, int num_pkts)
{
	(void) rbm;
	struct zc_rx_free_ring *ring = &mtcp->ctx->flash_ctx.zc_rx_ring;
	uint32_t tail = __atomic_load_n(&ring->tail, __ATOMIC_RELAXED);
	uint32_t head = __atomic_load_n(&ring->head, __ATOMIC_ACQUIRE);

	
	if (tail + num_pkts - head >= ZC_RX_FREE_RING_SIZE) {
		printf("not possible: ring size was taken too big\n");
		errno = EAGAIN;
		return -1;
	}

	for (int i = 0; i < num_pkts; i++) {
		ring->flash_addrs[(tail + i) & (ZC_RX_FREE_RING_SIZE - 1)] = addr_array[i];
	}

	__atomic_store_n(&ring->tail, tail + num_pkts, __ATOMIC_RELEASE);

	return 0;
}

#endif /* MTCP_RX_ZERO_COPY */
