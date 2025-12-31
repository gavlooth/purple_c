#include "hashmap.h"
#include <stdlib.h>
#include <string.h>

#define INITIAL_BUCKETS 64
#define MAX_LOAD_FACTOR 0.75f

// Hash function for pointers (uses FNV-1a style mixing)
static size_t hash_ptr(void* ptr) {
    uintptr_t x = (uintptr_t)ptr;
    // Mix the bits
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return (size_t)x;
}

// Create with default capacity
HashMap* hashmap_new(void) {
    return hashmap_with_capacity(INITIAL_BUCKETS);
}

// Create with specified capacity
HashMap* hashmap_with_capacity(size_t capacity) {
    HashMap* map = malloc(sizeof(HashMap));
    if (!map) return NULL;

    if (capacity < 16) capacity = 16;

    map->buckets = calloc(capacity, sizeof(HashEntry*));
    if (!map->buckets) {
        free(map);
        return NULL;
    }

    map->bucket_count = capacity;
    map->entry_count = 0;
    map->load_factor = MAX_LOAD_FACTOR;
    map->had_alloc_failure = 0;
    return map;
}

// Free hash map
void hashmap_free(HashMap* map) {
    if (!map) return;
    hashmap_free_entries(map);
    free(map->buckets);
    free(map);
}

// Free all entries
void hashmap_free_entries(HashMap* map) {
    if (!map) return;

    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            HashEntry* next = entry->next;
            free(entry);
            entry = next;
        }
        map->buckets[i] = NULL;
    }
    map->entry_count = 0;
}

// Resize when load factor exceeded
static void hashmap_resize(HashMap* map) {
    // Check for integer overflow before doubling
    if (map->bucket_count > SIZE_MAX / 2) {
        map->had_alloc_failure = 1;
        return;  // Cannot double, at max capacity
    }
    size_t new_count = map->bucket_count * 2;
    HashEntry** new_buckets = calloc(new_count, sizeof(HashEntry*));
    if (!new_buckets) {
        map->had_alloc_failure = 1;
        return;  // Keep old buckets on failure
    }

    // Rehash all entries
    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            HashEntry* next = entry->next;
            size_t idx = hash_ptr(entry->key) % new_count;
            entry->next = new_buckets[idx];
            new_buckets[idx] = entry;
            entry = next;
        }
    }

    free(map->buckets);
    map->buckets = new_buckets;
    map->bucket_count = new_count;
}

// Get value by key
void* hashmap_get(HashMap* map, void* key) {
    if (!map) return NULL;

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry* entry = map->buckets[idx];

    while (entry) {
        if (entry->key == key) return entry->value;
        entry = entry->next;
    }
    return NULL;
}

// Put key-value pair
void hashmap_put(HashMap* map, void* key, void* value) {
    if (!map) return;

    // Check if need to resize
    if ((float)map->entry_count / map->bucket_count > map->load_factor) {
        hashmap_resize(map);
    }

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry* entry = map->buckets[idx];

    // Check if key exists
    while (entry) {
        if (entry->key == key) {
            entry->value = value;
            return;
        }
        entry = entry->next;
    }

    // Create new entry
    entry = malloc(sizeof(HashEntry));
    if (!entry) {
        map->had_alloc_failure = 1;
        return;
    }

    entry->key = key;
    entry->value = value;
    entry->next = map->buckets[idx];
    map->buckets[idx] = entry;
    map->entry_count++;
}

// Remove key
void* hashmap_remove(HashMap* map, void* key) {
    if (!map) return NULL;

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry** prev = &map->buckets[idx];
    HashEntry* entry = *prev;

    while (entry) {
        if (entry->key == key) {
            void* value = entry->value;
            *prev = entry->next;
            free(entry);
            map->entry_count--;
            return value;
        }
        prev = &entry->next;
        entry = entry->next;
    }
    return NULL;
}

// Check if key exists
int hashmap_contains(HashMap* map, void* key) {
    if (!map) return 0;

    size_t idx = hash_ptr(key) % map->bucket_count;
    HashEntry* entry = map->buckets[idx];

    while (entry) {
        if (entry->key == key) return 1;
        entry = entry->next;
    }
    return 0;
}

// Iterate over all entries
void hashmap_foreach(HashMap* map, HashMapIterFn fn, void* ctx) {
    if (!map || !fn) return;

    for (size_t i = 0; i < map->bucket_count; i++) {
        HashEntry* entry = map->buckets[i];
        while (entry) {
            fn(entry->key, entry->value, ctx);
            entry = entry->next;
        }
    }
}

// Get entry count
size_t hashmap_size(HashMap* map) {
    return map ? map->entry_count : 0;
}

// Clear all entries
void hashmap_clear(HashMap* map) {
    hashmap_free_entries(map);
}

int hashmap_had_alloc_failure(HashMap* map) {
    return map ? map->had_alloc_failure : 0;
}
