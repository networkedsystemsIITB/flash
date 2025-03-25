#pragma once

#include <stdint.h>
#include <string.h>

static inline uint32_t murmurhash(const void *key, size_t key_size, uint32_t seed)
{
	const uint8_t *data = (const uint8_t *)key;
	const int nblocks = key_size / 4;
	uint32_t h = seed;
	uint32_t c1 = 0xcc9e2d51;
	uint32_t c2 = 0x1b873593;
	uint32_t k;

	// Process 4-byte chunks
	for (int i = 0; i < nblocks; i++) {
		memcpy(&k, data + (i * 4), 4);
		k *= c1;
		k = (k << 15) | (k >> 17);
		k *= c2;

		h ^= k;
		h = (h << 13) | (h >> 19);
		h = h * 5 + 0xe6546b64;
	}

	// Process remaining bytes
	k = 0;
	for (size_t i = key_size & ~3; i < key_size; i++) {
		k ^= data[i] << ((i & 3) * 8);
	}
	k *= c1;
	k = (k << 15) | (k >> 17);
	k *= c2;
	h ^= k;

	// Finalization
	h ^= key_size;
	h ^= h >> 16;
	h *= 0x85ebca6b;
	h ^= h >> 13;
	h *= 0xc2b2ae35;
	h ^= h >> 16;

	return h;
}