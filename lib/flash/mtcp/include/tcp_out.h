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

#ifndef TCP_OUT_H
#define TCP_OUT_H

#include "mtcp.h"
#include "tcp_stream.h"

enum ack_opt { ACK_OPT_NOW, ACK_OPT_AGGREGATE, ACK_OPT_WACK };

int SendTCPPacketStandalone(struct mtcp_manager *mtcp, uint32_t saddr, uint16_t sport, uint32_t daddr, uint16_t dport, uint32_t seq,
			    uint32_t ack_seq, uint16_t window, uint8_t flags, uint8_t *payload, uint16_t payloadlen, uint32_t cur_ts,
			    uint32_t echo_ts);

int SendTCPPacket(struct mtcp_manager *mtcp, tcp_stream *cur_stream, uint32_t cur_ts, uint8_t flags, uint8_t *payload,
		  uint16_t payloadlen);

extern inline int WriteTCPControlList(mtcp_manager_t mtcp, struct mtcp_sender *sender, uint32_t cur_ts, int thresh);

extern inline int WriteTCPDataList(mtcp_manager_t mtcp, struct mtcp_sender *sender, uint32_t cur_ts, int thresh);

extern inline int WriteTCPACKList(mtcp_manager_t mtcp, struct mtcp_sender *sender, uint32_t cur_ts, int thresh);

extern inline void AddtoControlList(mtcp_manager_t mtcp, tcp_stream *cur_stream, uint32_t cur_ts);

extern inline void AddtoSendList(mtcp_manager_t mtcp, tcp_stream *cur_stream);

extern inline void RemoveFromControlList(mtcp_manager_t mtcp, tcp_stream *cur_stream);

extern inline void RemoveFromSendList(mtcp_manager_t mtcp, tcp_stream *cur_stream);

extern inline void RemoveFromACKList(mtcp_manager_t mtcp, tcp_stream *cur_stream);

extern inline void EnqueueACK(mtcp_manager_t mtcp, tcp_stream *cur_stream, uint32_t cur_ts, uint8_t opt);

extern inline void DumpControlList(mtcp_manager_t mtcp, struct mtcp_sender *sender);

#endif /* TCP_OUT_H */
