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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/queue.h>

#include "debug.h"
#include "fhash.h"

#define IS_FLOW_TABLE(x) (x == HashFlow)
#define IS_LISTEN_TABLE(x) (x == HashListener)
#if USE_CCP
#define IS_SID_TABLE(x) (x == HashSID)
#endif

/*----------------------------------------------------------------------------*/
struct hashtable *CreateHashtable(unsigned int (*hashfn)(const void *),	   // key function
				  int (*eqfn)(const void *, const void *), // equality
				  int bins)				   // no of bins
{
	int i;
	struct hashtable *ht = calloc(1, sizeof(struct hashtable));
	if (!ht) {
		TRACE_ERROR("calloc: CreateHashtable");
		return 0;
	}

	ht->hashfn = hashfn;
	ht->eqfn = eqfn;
	ht->bins = bins;

	/* creating bins */
#if USE_CCP
	if (IS_FLOW_TABLE(hashfn) || IS_SID_TABLE(hashfn)) {
#else
	if (IS_FLOW_TABLE(hashfn)) {
#endif
		ht->ht_table = calloc(bins, sizeof(hash_bucket_head));
		if (!ht->ht_table) {
			TRACE_ERROR("calloc: CreateHashtable bins!\n");
			free(ht);
			return 0;
		}
		/* init the tables */
		for (i = 0; i < bins; i++)
			TAILQ_INIT(&ht->ht_table[i]);
	} else if (IS_LISTEN_TABLE(hashfn)) {
		ht->lt_table = calloc(bins, sizeof(list_bucket_head));
		if (!ht->lt_table) {
			TRACE_ERROR("calloc: CreateHashtable bins!\n");
			free(ht);
			return 0;
		}
		/* init the tables */
		for (i = 0; i < bins; i++)
			TAILQ_INIT(&ht->lt_table[i]);
	}

	return ht;
}
/*----------------------------------------------------------------------------*/
void DestroyHashtable(struct hashtable *ht)
{
	if (IS_FLOW_TABLE(ht->hashfn))
		free(ht->ht_table);
	else /* IS_LISTEN_TABLE(ht->hashfn) */
		free(ht->lt_table);
	free(ht);
}
/*----------------------------------------------------------------------------*/
int StreamHTInsert(struct hashtable *ht, void *it)
{
	/* create an entry*/
	int idx;
	tcp_stream *item = (tcp_stream *)it;

	assert(ht);

	idx = ht->hashfn(item);
	assert(idx >= 0 && idx < NUM_BINS_FLOWS);

	TAILQ_INSERT_TAIL(&ht->ht_table[idx], item, rcvvar->he_link);

	item->ht_idx = TCP_AR_CNT;

	return 0;
}
/*----------------------------------------------------------------------------*/
void *StreamHTRemove(struct hashtable *ht, void *it)
{
	hash_bucket_head *head;
	tcp_stream *item = (tcp_stream *)it;
	int idx = ht->hashfn(item);

	head = &ht->ht_table[idx];
	TAILQ_REMOVE(head, item, rcvvar->he_link);

	return (item);
}
/*----------------------------------------------------------------------------*/
void *StreamHTSearch(struct hashtable *ht, const void *it)
{
	int idx;
	const tcp_stream *item = (const tcp_stream *)it;
	tcp_stream *walk;
	hash_bucket_head *head;

	idx = ht->hashfn(item);

	head = &ht->ht_table[ht->hashfn(item)];
	TAILQ_FOREACH(walk, head, rcvvar->he_link)
	{
		if (ht->eqfn(walk, item))
			return walk;
	}

	UNUSED(idx);
	return NULL;
}
/*----------------------------------------------------------------------------*/
unsigned int HashListener(const void *l)
{
	const struct tcp_listener *listener = (const struct tcp_listener *)l;

	return listener->socket->saddr.sin_port & (NUM_BINS_LISTENERS - 1);
}
/*----------------------------------------------------------------------------*/
int EqualListener(const void *l1, const void *l2)
{
	const struct tcp_listener *listener1 = (const struct tcp_listener *)l1;
	const struct tcp_listener *listener2 = (const struct tcp_listener *)l2;

	return (listener1->socket->saddr.sin_port == listener2->socket->saddr.sin_port);
}
/*----------------------------------------------------------------------------*/
int ListenerHTInsert(struct hashtable *ht, void *it)
{
	/* create an entry*/
	int idx;
	struct tcp_listener *item = (struct tcp_listener *)it;

	assert(ht);

	idx = ht->hashfn(item);
	assert(idx >= 0 && idx < NUM_BINS_LISTENERS);

	TAILQ_INSERT_TAIL(&ht->lt_table[idx], item, he_link);

	return 0;
}
/*----------------------------------------------------------------------------*/
void *ListenerHTRemove(struct hashtable *ht, void *it)
{
	list_bucket_head *head;
	struct tcp_listener *item = (struct tcp_listener *)it;
	int idx = ht->hashfn(item);

	head = &ht->lt_table[idx];
	TAILQ_REMOVE(head, item, he_link);

	return (item);
}
/*----------------------------------------------------------------------------*/
void *ListenerHTSearch(struct hashtable *ht, const void *it)
{
	int idx;
	struct tcp_listener item;
	uint16_t port = *((const uint16_t *)it);
	struct tcp_listener *walk;
	list_bucket_head *head;
	struct socket_map s;

	s.saddr.sin_port = port;
	item.socket = &s;

	idx = ht->hashfn(&item);

	head = &ht->lt_table[idx];
	TAILQ_FOREACH(walk, head, he_link)
	{
		if (ht->eqfn(walk, &item))
			return walk;
	}

	return NULL;
}
/*----------------------------------------------------------------------------*/
