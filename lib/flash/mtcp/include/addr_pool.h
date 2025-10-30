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

#ifndef ADDR_POOL_H
#define ADDR_POOL_H

#include <netinet/in.h>
#include <sys/queue.h>

#define MIN_PORT (1025)
#define MAX_PORT (65535 + 1)
/*----------------------------------------------------------------------------*/
typedef struct addr_pool *addr_pool_t;
/*----------------------------------------------------------------------------*/
/* CreateAddressPool()                                                        */
/* Create address pool for given address range.                               */
/* addr_base: the base address in network order.                              */
/* num_addr: number of addresses to use as source IP                          */
/*----------------------------------------------------------------------------*/
addr_pool_t CreateAddressPool(in_addr_t addr_base, int num_addr);
/*----------------------------------------------------------------------------*/
/* CreateAddressPoolPerCore()                                                 */
/* Create address pool only for the given core number.                        */
/* All addresses and port numbers should be in network order.                 */
/*----------------------------------------------------------------------------*/
addr_pool_t CreateAddressPoolPerCore(int core, int num_queues, in_addr_t saddr_base, int num_addr, in_addr_t daddr, in_port_t dport);
/*----------------------------------------------------------------------------*/
void DestroyAddressPool(addr_pool_t ap);
/*----------------------------------------------------------------------------*/
int FetchAddress(addr_pool_t ap, int core, int num_queues, const struct sockaddr_in *daddr, struct sockaddr_in *saddr);
/*----------------------------------------------------------------------------*/
int FetchAddressPerCore(addr_pool_t ap, int core, int num_queues, const struct sockaddr_in *daddr, struct sockaddr_in *saddr);
/*----------------------------------------------------------------------------*/
int FreeAddress(addr_pool_t ap, const struct sockaddr_in *addr);
/*----------------------------------------------------------------------------*/

#endif /* ADDR_POOL_H */
