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

#ifndef TCP_STREAM_QUEUE
#define TCP_STREAM_QUEUE

#include <stdint.h>

#ifndef LOCK_STREAM_QUEUE
#define LOCK_STREAM_QUEUE 0
#endif

/* Lock definitions for stream queue */
#if LOCK_STREAM_QUEUE

#if USE_SPIN_LOCK
#define SQ_LOCK_INIT(lock, errmsg, action)                      \
	;                                                       \
	if (pthread_spin_init(lock, PTHREAD_PROCESS_PRIVATE)) { \
		perror("pthread_spin_init" errmsg);             \
		action;                                         \
	}
#define SQ_LOCK_DESTROY(lock) pthread_spin_destroy(lock)
#define SQ_LOCK(lock) pthread_spin_lock(lock)
#define SQ_UNLOCK(lock) pthread_spin_unlock(lock)
#else
#define SQ_LOCK_INIT(lock, errmsg, action)           \
	;                                            \
	if (pthread_mutex_init(lock, NULL)) {        \
		perror("pthread_mutex_init" errmsg); \
		action;                              \
	}
#define SQ_LOCK_DESTROY(lock) pthread_mutex_destroy(lock)
#define SQ_LOCK(lock) pthread_mutex_lock(lock)
#define SQ_UNLOCK(lock) pthread_mutex_unlock(lock)
#endif /* USE_SPIN_LOCK */

#else /* LOCK_STREAM_QUEUE */
#define SQ_LOCK_INIT(lock, errmsg, action) (void)0
#define SQ_LOCK_DESTROY(lock) (void)0
#define SQ_LOCK(lock) (void)0
#define SQ_UNLOCK(lock) (void)0
#endif /* LOCK_STREAM_QUEUE */

/*---------------------------------------------------------------------------*/
typedef struct stream_queue *stream_queue_t;
/*---------------------------------------------------------------------------*/
typedef struct stream_queue_int {
	struct tcp_stream **array;
	int size;

	int first;
	int last;
	int count;

} stream_queue_int;
/*---------------------------------------------------------------------------*/
stream_queue_int *CreateInternalStreamQueue(int size);
/*---------------------------------------------------------------------------*/
void DestroyInternalStreamQueue(stream_queue_int *sq);
/*---------------------------------------------------------------------------*/
int StreamInternalEnqueue(stream_queue_int *sq, struct tcp_stream *stream);
/*---------------------------------------------------------------------------*/
struct tcp_stream *StreamInternalDequeue(stream_queue_int *sq);
/*---------------------------------------------------------------------------*/
stream_queue_t CreateStreamQueue(int size);
/*---------------------------------------------------------------------------*/
void DestroyStreamQueue(stream_queue_t sq);
/*---------------------------------------------------------------------------*/
int StreamEnqueue(stream_queue_t sq, struct tcp_stream *stream);
/*---------------------------------------------------------------------------*/
struct tcp_stream *StreamDequeue(stream_queue_t sq);
/*---------------------------------------------------------------------------*/
int StreamQueueIsEmpty(stream_queue_t sq);
/*---------------------------------------------------------------------------*/

#endif /* TCP_STREAM_QUEUE */
