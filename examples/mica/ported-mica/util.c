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

#include "util.h"

size_t mehcached_next_power_of_two(size_t v)
{
	size_t s = 0;
	while (((size_t)1 << s) < v)
		s++;
	return (size_t)1 << s;
}

void memory_barrier(void)
{
	asm volatile("" ::: "memory");
}

void mehcached_memcpy8(uint8_t *dest, const uint8_t *src, size_t length)
{
	length = MEHCACHED_ROUNDUP8(length);
	switch (length >> 3) {
	case 0:
		break;
	case 1:
		*(uint64_t *)(dest + 0) = *(const uint64_t *)(src + 0);
		break;
	case 2:
		*(uint64_t *)(dest + 0) = *(const uint64_t *)(src + 0);
		*(uint64_t *)(dest + 8) = *(const uint64_t *)(src + 8);
		break;
	case 3:
		*(uint64_t *)(dest + 0) = *(const uint64_t *)(src + 0);
		*(uint64_t *)(dest + 8) = *(const uint64_t *)(src + 8);
		*(uint64_t *)(dest + 16) = *(const uint64_t *)(src + 16);
		break;
	case 4:
		*(uint64_t *)(dest + 0) = *(const uint64_t *)(src + 0);
		*(uint64_t *)(dest + 8) = *(const uint64_t *)(src + 8);
		*(uint64_t *)(dest + 16) = *(const uint64_t *)(src + 16);
		*(uint64_t *)(dest + 24) = *(const uint64_t *)(src + 24);
		break;
	default:
		memcpy(dest, src, length);
		break;
	}
}

bool mehcached_memcmp8(const uint8_t *dest, const uint8_t *src, size_t length)
{
	length = MEHCACHED_ROUNDUP8(length);
	switch (length >> 3) {
	case 0:
		return true;
	case 1:
		if (*(const uint64_t *)(dest + 0) != *(const uint64_t *)(src + 0))
			return false;
		return true;
	case 2:
		if (*(const uint64_t *)(dest + 0) != *(const uint64_t *)(src + 0))
			return false;
		if (*(const uint64_t *)(dest + 8) != *(const uint64_t *)(src + 8))
			return false;
		return true;
	case 3:
		if (*(const uint64_t *)(dest + 0) != *(const uint64_t *)(src + 0))
			return false;
		if (*(const uint64_t *)(dest + 8) != *(const uint64_t *)(src + 8))
			return false;
		if (*(const uint64_t *)(dest + 16) != *(const uint64_t *)(src + 16))
			return false;
		return true;
	case 4:
		if (*(const uint64_t *)(dest + 0) != *(const uint64_t *)(src + 0))
			return false;
		if (*(const uint64_t *)(dest + 8) != *(const uint64_t *)(src + 8))
			return false;
		if (*(const uint64_t *)(dest + 16) != *(const uint64_t *)(src + 16))
			return false;
		if (*(const uint64_t *)(dest + 24) != *(const uint64_t *)(src + 24))
			return false;
		return true;
	default:
		return memcmp(dest, src, length) == 0;
	}
}