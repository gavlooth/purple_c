#ifndef PURPLE_HASHMAP_H
#define PURPLE_HASHMAP_H

#include <stddef.h>
#include <stdint.h>

// Simple pointer-keyed hash map for O(1) lookups
// Used for fast object->node mappings in SCC and deferred RC

typedef struct HashEntry {
    void* key;
    void* value;
    struct HashEntry* next;  // For collision chaining
} HashEntry;

typedef struct HashMap {
    HashEntry** buckets;
    size_t bucket_count;
    size_t entry_count;
    float load_factor;
    int had_alloc_failure;
} HashMap;

// Create/destroy
HashMap* hashmap_new(void);
HashMap* hashmap_with_capacity(size_t capacity);
void hashmap_free(HashMap* map);
void hashmap_free_entries(HashMap* map);  // Free entries but not values

// Operations (pointer keys)
void* hashmap_get(HashMap* map, void* key);
void hashmap_put(HashMap* map, void* key, void* value);
void* hashmap_remove(HashMap* map, void* key);
int hashmap_contains(HashMap* map, void* key);

// Iteration
typedef void (*HashMapIterFn)(void* key, void* value, void* ctx);
void hashmap_foreach(HashMap* map, HashMapIterFn fn, void* ctx);

// Utility
size_t hashmap_size(HashMap* map);
void hashmap_clear(HashMap* map);
int hashmap_had_alloc_failure(HashMap* map);

#endif // PURPLE_HASHMAP_H
