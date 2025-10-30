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

#include "clock.h"
/*----------------------------------------------------------------------------*/
uint64_t init_time_ns = 0;
uint32_t last_print = 0;
/*----------------------------------------------------------------------------*/
uint64_t now_usecs()
{
	struct timespec now;
	uint64_t now_ns, now_us;

	clock_gettime(CLOCK_MONOTONIC, &now);

	now_ns = (1000000000L * now.tv_sec) + now.tv_nsec;
	if (init_time_ns == 0) {
		init_time_ns = now_ns;
	}

	now_us = ((now_ns - init_time_ns) / 1000) & 0xffffffff;
	return now_us;
}
/*----------------------------------------------------------------------------*/
uint64_t time_since_usecs(uint64_t then)
{
	return now_usecs() - then;
}
/*----------------------------------------------------------------------------*/
uint64_t time_after_usecs(uint64_t usecs)
{
	return now_usecs() + usecs;
}
/*----------------------------------------------------------------------------*/
#define SAMPLE_FREQ_US 10000

void log_cwnd_rtt(void *vs)
{
	tcp_stream *stream = (tcp_stream *)vs;
	unsigned long now = (unsigned long)(now_usecs());
	if (time_since_usecs(last_print) > SAMPLE_FREQ_US) {
		fprintf(stderr, "%lu %d %d/%d\n", now / 1000, stream->rcvvar->srtt * 125, stream->sndvar->cwnd / stream->sndvar->mss,
			stream->sndvar->peer_wnd / stream->sndvar->mss);
#if RATE_LIMIT_ENABLED
		PrintBucket(stream->bucket);
#endif
#if PACING_ENABLED
		PrintPacer(stream->pacer);
#endif
		last_print = now;
	}
}
/*----------------------------------------------------------------------------*/
