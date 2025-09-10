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

#ifndef EVENTPOLL_H
#define EVENTPOLL_H

#include "mtcp_api.h"
#include "mtcp_epoll.h"

/*----------------------------------------------------------------------------*/
struct mtcp_epoll_stat {
	uint64_t calls;
	uint64_t waits;
	uint64_t wakes;

	uint64_t issued;
	uint64_t registered;
	uint64_t invalidated;
	uint64_t handled;
};
/*----------------------------------------------------------------------------*/
struct mtcp_epoll_event_int {
	struct mtcp_epoll_event ev;
	int sockid;
};
/*----------------------------------------------------------------------------*/
enum event_queue_type { USR_EVENT_QUEUE = 0, USR_SHADOW_EVENT_QUEUE = 1, MTCP_EVENT_QUEUE = 2 };
/*----------------------------------------------------------------------------*/
struct event_queue {
	struct mtcp_epoll_event_int *events;
	int start; // starting index
	int end;   // ending index

	int size;	// max size
	int num_events; // number of events
};
/*----------------------------------------------------------------------------*/
struct mtcp_epoll {
	struct event_queue *usr_queue;
	struct event_queue *usr_shadow_queue;
	struct event_queue *mtcp_queue;

	uint8_t waiting;
	struct mtcp_epoll_stat stat;

	pthread_cond_t epoll_cond;
	pthread_mutex_t epoll_lock;
};
/*----------------------------------------------------------------------------*/

int CloseEpollSocket(mctx_t mctx, int epid);

#endif /* EVENTPOLL_H */
