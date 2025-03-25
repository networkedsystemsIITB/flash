#include "hashmap.h"
#include <string.h>
#include <stdio.h>

// Hash function using jhash
static inline int hashmap_hash(struct hashmap *map, void *key)
{
	return murmurhash(key, map->key_size, 0) % map->size;
}

// Initialize hashmap
int hashmap_init(struct hashmap *map, size_t key_size, size_t value_size, size_t max_entries)
{
	map = malloc(sizeof(struct hashmap));
	if (!map)
		return 0;

	map->size = max_entries;
	map->key_size = key_size;
	map->value_size = value_size;
	map->buckets = calloc(max_entries, sizeof(struct hashmap_entry *));

	if (!map->buckets) {
		free(map);
		return 0;
	}
	return 1;
}

// Insert a new key-value pair
int hashmap_insert_elem(struct hashmap *map, void *key, void *value)
{
	int hash = hashmap_hash(map, key);
	struct hashmap_entry *entry = map->buckets[hash];

	// Check if key exists (skip if already present)
	while (entry) {
		if (memcmp(entry->key, key, map->key_size) == 0) {
			return 0; // Key already exists, do nothing
		}
		entry = entry->next;
	}

	// Allocate new entry
	entry = malloc(sizeof(struct hashmap_entry));
	entry->key = malloc(map->key_size);
	entry->value = malloc(map->value_size);

	memcpy(entry->key, key, map->key_size);
	memcpy(entry->value, value, map->value_size);

	entry->next = map->buckets[hash];
	map->buckets[hash] = entry;
    return 1;
}

// Lookup a value by key
void *hashmap_lookup_elem(struct hashmap *map, void *key)
{
	int hash = hashmap_hash(map, key);
	struct hashmap_entry *entry = map->buckets[hash];

	while (entry) {
		if (memcmp(entry->key, key, map->key_size) == 0) {
			return entry->value;
		}
		entry = entry->next;
	}
	return NULL; // Key not found
}

// Update an existing key with a new value
void hashmap_update_elem(struct hashmap *map, void *key, void *value)
{
	int hash = hashmap_hash(map, key);
	struct hashmap_entry *entry = map->buckets[hash];

	while (entry) {
		if (memcmp(entry->key, key, map->key_size) == 0) {
			memcpy(entry->value, value, map->value_size); // Update value
			return;
		}
		entry = entry->next;
	}
}

// Delete a key from the hashmap
void hashmap_delete_elem(struct hashmap *map, void *key)
{
	int hash = hashmap_hash(map, key);
	struct hashmap_entry **entry_ptr = &map->buckets[hash];

	while (*entry_ptr) {
		struct hashmap_entry *entry = *entry_ptr;
		if (memcmp(entry->key, key, map->key_size) == 0) {
			*entry_ptr = entry->next;
			free(entry->key);
			free(entry->value);
			free(entry);
			return;
		}
		entry_ptr = &entry->next;
	}
}

// Free the hashmap
void hashmap_free(struct hashmap *map)
{
	for (size_t i = 0; i < map->size; i++) {
		struct hashmap_entry *entry = map->buckets[i];
		while (entry) {
			struct hashmap_entry *tmp = entry;
			entry = entry->next;
			free(tmp->key);
			free(tmp->value);
			free(tmp);
		}
	}
	free(map->buckets);
	free(map);
}
