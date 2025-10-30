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

#ifndef STAT_H
#define STAT_H

struct run_stat {
	uint64_t rounds;
	uint64_t rounds_rx;
	uint64_t rounds_rx_try;
	uint64_t rounds_tx;
	uint64_t rounds_tx_try;
	uint64_t rounds_select;
	uint64_t rounds_select_rx;
	uint64_t rounds_select_tx;
	uint64_t rounds_select_intr;

	uint64_t rounds_accept;
	uint64_t rounds_read;
	uint64_t rounds_write;
	uint64_t rounds_epoll;
	uint64_t rounds_wndadv;

	uint64_t rounds_rtocheck;
	uint64_t rounds_twcheck;
	uint64_t rounds_tocheck;
};

struct stat_counter {
	uint64_t cnt;
	uint64_t sum;
	uint64_t max;
	uint64_t min;
};

struct time_stat {
	struct stat_counter round;
	struct stat_counter processing;
	struct stat_counter tcheck;
	struct stat_counter epoll;
	struct stat_counter handle;
	struct stat_counter xmit;
	struct stat_counter select;
};

struct net_stat {
	uint64_t tx_packets[MAX_DEVICES];
	uint64_t tx_bytes[MAX_DEVICES];
	uint64_t tx_drops[MAX_DEVICES];
	uint64_t rx_packets[MAX_DEVICES];
	uint64_t rx_bytes[MAX_DEVICES];
	uint64_t rx_errors[MAX_DEVICES];
#ifdef ENABLELRO
	uint64_t tx_gdptbytes;
	uint64_t rx_gdptbytes;
#endif
};

struct bcast_stat {
	uint64_t cycles;
	uint64_t write;
	uint64_t read;
	uint64_t epoll;
	uint64_t wnd_adv;
	uint64_t ack;
};

struct timeout_stat {
	uint64_t cycles;
	uint64_t rto_try;
	uint64_t rto;
	uint64_t timewait_try;
	uint64_t timewait;
};

#ifdef NETSTAT
#define STAT_COUNT(stat) stat++
#else
#define STAT_COUNT(stat)
#endif

#endif /* STAT_H */
