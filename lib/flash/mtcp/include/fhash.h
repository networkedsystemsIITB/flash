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

#ifndef FHASH_H
#define FHASH_H

#include <sys/queue.h>
#include "tcp_stream.h"

#define NUM_BINS_FLOWS (131072)	  /* 132 K entries per thread*/
#define NUM_BINS_LISTENERS (1024) /* assuming that chaining won't happen excessively */
#define TCP_AR_CNT (3)

typedef struct hash_bucket_head {
	tcp_stream *tqh_first;
	tcp_stream **tqh_last;
} hash_bucket_head;

typedef struct list_bucket_head {
	struct tcp_listener *tqh_first;
	struct tcp_listener **tqh_last;
} list_bucket_head;

/* hashtable structure */
struct hashtable {
	uint32_t bins;

	union {
		hash_bucket_head *ht_table;
		list_bucket_head *lt_table;
	};

	// functions
	unsigned int (*hashfn)(const void *);
	int (*eqfn)(const void *, const void *);
};

/*functions for hashtable*/
struct hashtable *CreateHashtable(unsigned int (*hashfn)(const void *), int (*eqfn)(const void *, const void *), int bins);
void DestroyHashtable(struct hashtable *ht);

int StreamHTInsert(struct hashtable *ht, void *);
void *StreamHTRemove(struct hashtable *ht, void *);
void *StreamHTSearch(struct hashtable *ht, const void *);
unsigned int HashListener(const void *hbo_port_ptr);
int EqualListener(const void *hbo_port_ptr1, const void *hbo_port_ptr2);
int ListenerHTInsert(struct hashtable *ht, void *);
void *ListenerHTRemove(struct hashtable *ht, void *);
void *ListenerHTSearch(struct hashtable *ht, const void *);

#endif /* FHASH_H */
