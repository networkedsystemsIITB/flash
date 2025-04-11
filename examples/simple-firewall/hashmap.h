#ifndef HASHMAP_H
#define HASHMAP_H

#include <stdlib.h>
#include "murmurhash.h"

struct hashmap_entry {
	void *key;
	void *value;
	struct hashmap_entry *next;
};

struct hashmap {
	struct hashmap_entry **buckets;
	size_t size;	   // Number of buckets
	size_t key_size;   // Size of each key
	size_t value_size; // Size of each value
};

// Function prototypes
int hashmap_init(struct hashmap *map, size_t key_size, size_t value_size, size_t max_entries);
int hashmap_insert_elem(struct hashmap *map, void *key, void *value);
void *hashmap_lookup_elem(struct hashmap *map, void *key);
void hashmap_update_elem(struct hashmap *map, void *key, void *value);
void hashmap_delete_elem(struct hashmap *map, void *key);
void hashmap_free(struct hashmap *map);

#endif // HASHMAP_H
