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

#ifndef TCP_SEND_BUFFER_H
#define TCP_SEND_BUFFER_H

#include <stdlib.h>
#include <stdint.h>

/*----------------------------------------------------------------------------*/
typedef struct sb_manager *sb_manager_t;
typedef struct mtcp_manager *mtcp_manager_t;
/*----------------------------------------------------------------------------*/
struct tcp_send_buffer {
	unsigned char *data;
	unsigned char *head;

	uint32_t head_off;
	uint32_t tail_off;
	uint32_t len;
	uint64_t cum_len;
	uint32_t size;

	uint32_t head_seq;
	uint32_t init_seq;
};
/*----------------------------------------------------------------------------*/
uint32_t SBGetCurnum(sb_manager_t sbm);
/*----------------------------------------------------------------------------*/
sb_manager_t SBManagerCreate(mtcp_manager_t mtcp, size_t chunk_size, uint32_t cnum);
/*----------------------------------------------------------------------------*/
struct tcp_send_buffer *SBInit(sb_manager_t sbm, uint32_t init_seq);
/*----------------------------------------------------------------------------*/
void SBFree(sb_manager_t sbm, struct tcp_send_buffer *buf);
/*----------------------------------------------------------------------------*/
size_t SBPut(sb_manager_t sbm, struct tcp_send_buffer *buf, const void *data, size_t len);
/*----------------------------------------------------------------------------*/
size_t SBRemove(sb_manager_t sbm, struct tcp_send_buffer *buf, size_t len);
/*----------------------------------------------------------------------------*/

#endif /* TCP_SEND_BUFFER_H */
