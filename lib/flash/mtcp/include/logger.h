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

#ifndef LOGGER_H
#define LOGGER_H

#include <stdint.h>

#define LOG_BUFF_SIZE (256 * 1024)
#define NUM_LOG_BUFF (100)

extern enum { IDLE_LOGT, ACTIVE_LOGT } log_thread_state;

typedef struct log_buff {
	int tid;
	FILE *fid;
	int buff_len;
	char buff[LOG_BUFF_SIZE];
	TAILQ_ENTRY(log_buff) buff_link;
} log_buff;

typedef struct log_thread_context {
	pthread_t thread;
	int cpu;
	int done;
	int sp_fd;
	int pair_sp_fd;
	int free_buff_cnt;
	int job_buff_cnt;

	uint8_t state;

	pthread_mutex_t mutex;
	pthread_mutex_t free_mutex;

	TAILQ_HEAD(, log_buff) working_queue;
	TAILQ_HEAD(, log_buff) free_queue;

} log_thread_context;

log_buff *DequeueFreeBuffer(log_thread_context *ctx);
void EnqueueJobBuffer(log_thread_context *ctx, log_buff *working_bp);
void InitLogThreadContext(log_thread_context *ctx, int cpu);
void *ThreadLogMain(void *arg);

#endif /* LOGGER_H */
