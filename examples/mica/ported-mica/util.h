// Copyright 2014 Carnegie Mellon University
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include "common.h"

#include <sys/time.h>
#include <sys/resource.h>

size_t mehcached_next_power_of_two(size_t v);
void memory_barrier(void);
void mehcached_memcpy8(uint8_t *dest, const uint8_t *src, size_t length);
bool mehcached_memcmp8(const uint8_t *dest, const uint8_t *src, size_t length);

#define MEHCACHED_ROUNDUP8(x) (((x) + 7UL) & (~7UL))
#define MEHCACHED_ROUNDUP64(x) (((x) + 63UL) & (~63UL))
#define MEHCACHED_ROUNDUP4K(x) (((x) + 4095UL) & (~4095UL))
#define MEHCACHED_ROUNDUP1M(x) (((x) + 1048575UL) & (~1048575UL))
#define MEHCACHED_ROUNDUP2M(x) (((x) + 2097151UL) & (~2097151UL))
