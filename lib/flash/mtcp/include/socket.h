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

#ifndef SOCKET_H
#define SOCKET_H

#include "mtcp_api.h"
#include "mtcp_epoll.h"

/*----------------------------------------------------------------------------*/
enum socket_opts {
	MTCP_NONBLOCK = 0x01,
	MTCP_ADDR_BIND = 0x02,
};
/*----------------------------------------------------------------------------*/
struct socket_map {
	int id;
	int socktype;
	uint32_t opts;

	struct sockaddr_in saddr;

	union {
		struct tcp_stream *stream;
		struct tcp_listener *listener;
		struct mtcp_epoll *ep;
		struct pipe *pp;
	};

	uint32_t epoll;	 /* registered events */
	uint32_t events; /* available events */
	mtcp_epoll_data_t ep_data;

	TAILQ_ENTRY(socket_map) free_smap_link;
};
/*----------------------------------------------------------------------------*/
typedef struct socket_map *socket_map_t;
/*----------------------------------------------------------------------------*/
socket_map_t AllocateSocket(mctx_t mctx, int socktype, int need_lock);
/*----------------------------------------------------------------------------*/
void FreeSocket(mctx_t mctx, int sockid, int need_lock);
/*----------------------------------------------------------------------------*/
socket_map_t GetSocket(mctx_t mctx, int sockid);
/*----------------------------------------------------------------------------*/
struct tcp_listener {
	int sockid;
	socket_map_t socket;

	int backlog;
	stream_queue_t acceptq;

	pthread_mutex_t accept_lock;
	pthread_cond_t accept_cond;

	TAILQ_ENTRY(tcp_listener) he_link; /* hash table entry link */
};
/*----------------------------------------------------------------------------*/

#endif /* SOCKET_H */
