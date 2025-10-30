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

#ifndef __CCP_H_
#define __CCP_H_

#include <sys/un.h>

#include "tcp_stream.h"
#include "tcp_in.h"
#include "debug.h"

// CCP currently only supports a single global datapath and CCP instance, but
// this ID exists in case there is a need for supporting multiple
// If this change is made in the future CCP_UNIX_BASE_ID will need to be
// generated dynamically based on the CCP/datapath ID. For now, we always use 0.
#define CCP_UNIX_BASE "/tmp/ccp/"
#define CCP_ID "0/"
#define FROM_CCP "out"
#define TO_CCP "in"
#define FROM_CCP_PATH CCP_UNIX_BASE CCP_ID FROM_CCP
#define TO_CCP_PATH CCP_UNIX_BASE CCP_ID TO_CCP
#define CCP_MAX_MSG_SIZE 32678

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define EVENT_DUPACK 1
#define EVENT_TRI_DUPACK 2
#define EVENT_TIMEOUT 3
#define EVENT_ECN 4

void setup_ccp_connection(mtcp_manager_t mtcp);
void setup_ccp_send_socket(mtcp_manager_t mtcp);
void destroy_ccp_connection(mtcp_manager_t mtcp);
void ccp_create(mtcp_manager_t mtcp, tcp_stream *stream);
void ccp_cong_control(mtcp_manager_t mtcp, tcp_stream *stream, uint32_t ack, uint64_t bytes_delivered, uint64_t packets_delivered);
void ccp_record_event(mtcp_manager_t mtcp, tcp_stream *stream, uint8_t event_type, uint32_t val);

#endif
