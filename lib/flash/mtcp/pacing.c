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

#include "pacing.h"
#include "clock.h"
#include "tcp_util.h"
/*----------------------------------------------------------------------------*/
#if RATE_LIMIT_ENABLED
token_bucket *NewTokenBucket()
{
	token_bucket *bucket;
	bucket = malloc(sizeof(token_bucket));
	if (bucket == NULL)
		return NULL;

	bucket->rate = 0;
	bucket->burst = (MSS * INIT_CWND_PKTS);
	bucket->tokens = bucket->burst;
	bucket->last_fill_t = now_usecs();
	return bucket;
}
/*----------------------------------------------------------------------------*/
void _refill_bucket(token_bucket *bucket)
{
	uint32_t elapsed = time_since_usecs(bucket->last_fill_t);
	double new_tokens = SECONDS_TO_USECS(bucket->rate * elapsed);
	double prev_tokens = bucket->tokens;
	bucket->tokens = MIN(bucket->burst, bucket->tokens + new_tokens);
	if (bucket->tokens > prev_tokens) {
		bucket->last_fill_t = now_usecs();
	} else {
		//fprintf(stderr, "elapsed=%lu new=%f\n", time_since_usecs(bucket->last_fill_t), new_tokens);
	}
}
/*----------------------------------------------------------------------------*/
int SufficientTokens(token_bucket *bucket, uint64_t new_bits)
{
	double new_bytes = BITS_TO_BYTES(new_bits);

	//fprintf(stderr, "checking for %ld tokens\n", new_bits);

	_refill_bucket(bucket);

	if (bucket->tokens >= new_bytes) {
		bucket->tokens -= new_bytes;
		return 0;
	}

	return -1;
}
/*----------------------------------------------------------------------------*/
void PrintBucket(token_bucket *bucket)
{
	fprintf(stderr, "[rate=%.3f tokens=%f last=%u]\n", BPS_TO_MBPS(bucket->rate), bucket->tokens, bucket->last_fill_t);
}
/*----------------------------------------------------------------------------*/
#endif /* !RATE_LIMIT_ENABLED */

#if PACING_ENABLED
/*----------------------------------------------------------------------------*/
packet_pacer *NewPacketPacer()
{
	packet_pacer *pacer;
	pacer = malloc(sizeof(packet_pacer));
	if (pacer == NULL)
		return NULL;
	pacer->rate_bps = 0;
	pacer->extra_packets = 1;
	pacer->next_send_time = 0;
	return pacer;
}
/*----------------------------------------------------------------------------*/
int CanSendNow(packet_pacer *pacer)
{
	if (pacer->rate_bps == 0) {
		return TRUE;
	}

	uint32_t now = now_usecs();
	if (now >= pacer->next_send_time) {
		pacer->next_send_time = now + (int)(MSS / BPS_TO_MBPS(pacer->rate_bps));
		pacer->extra_packets = 1;
		//fprintf(stderr, "now=%u, next=%u\n", now, pacer->next_send_time);

		return TRUE;
	} else if (pacer->extra_packets) {
		pacer->extra_packets--;
		return TRUE;
	} else {
		return FALSE;
	}
}
/*----------------------------------------------------------------------------*/
void PrintPacer(packet_pacer *pacer)
{
	//fprintf(stderr, "[rate=%u next_time=%u]\n", pacer->rate_bps, pacer->next_send_time);
}
/*----------------------------------------------------------------------------*/
#endif /* !PACING_ENABLED */
