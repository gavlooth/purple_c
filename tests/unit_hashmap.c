// Unit tests for hashmap.c - targeting 90%+ coverage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/util/hashmap.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Test basic lifecycle
void test_hashmap_create_destroy(void) {
    TEST(hashmap_create_destroy);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }
    if (hashmap_size(map) != 0) { FAIL("new map should be empty"); hashmap_free(map); return; }

    hashmap_free(map);

    // Test with custom capacity
    HashMap* map2 = hashmap_with_capacity(64);
    if (!map2) { FAIL("hashmap_with_capacity returned NULL"); return; }
    hashmap_free(map2);

    // Test with small capacity (should normalize to minimum)
    HashMap* map3 = hashmap_with_capacity(4);
    if (!map3) { FAIL("small capacity returned NULL"); return; }
    hashmap_free(map3);

    PASS();
}

// Test put and get
void test_hashmap_put_get(void) {
    TEST(hashmap_put_get);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    int key1 = 42;
    char* val1 = "hello";

    hashmap_put(map, &key1, val1);
    if (hashmap_size(map) != 1) { FAIL("size should be 1"); hashmap_free(map); return; }

    char* result = (char*)hashmap_get(map, &key1);
    if (result != val1) { FAIL("get returned wrong value"); hashmap_free(map); return; }

    // Update existing key
    char* val2 = "world";
    hashmap_put(map, &key1, val2);
    if (hashmap_size(map) != 1) { FAIL("size should still be 1 after update"); hashmap_free(map); return; }

    result = (char*)hashmap_get(map, &key1);
    if (result != val2) { FAIL("get after update returned wrong value"); hashmap_free(map); return; }

    // Get non-existent key
    int key2 = 99;
    result = (char*)hashmap_get(map, &key2);
    if (result != NULL) { FAIL("get non-existent should return NULL"); hashmap_free(map); return; }

    hashmap_free(map);
    PASS();
}

// Test collision handling (multiple keys in same bucket)
void test_hashmap_collision(void) {
    TEST(hashmap_collision);

    HashMap* map = hashmap_with_capacity(16);  // Small to increase collision chance
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    // Insert many keys to force collisions
    int keys[100];
    for (int i = 0; i < 100; i++) {
        keys[i] = i;
        hashmap_put(map, &keys[i], &keys[i]);
    }

    if (hashmap_size(map) != 100) { FAIL("size should be 100"); hashmap_free(map); return; }

    // Verify all keys are retrievable
    for (int i = 0; i < 100; i++) {
        int* result = (int*)hashmap_get(map, &keys[i]);
        if (!result || *result != i) { FAIL("collision retrieval failed"); hashmap_free(map); return; }
    }

    hashmap_free(map);
    PASS();
}

// Test resize (load factor trigger)
void test_hashmap_resize(void) {
    TEST(hashmap_resize);

    HashMap* map = hashmap_with_capacity(16);
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    // Insert enough to trigger resize (load factor > 0.75)
    int keys[50];
    for (int i = 0; i < 50; i++) {
        keys[i] = i * 7;  // Spread out keys
        hashmap_put(map, &keys[i], &keys[i]);
    }

    // Verify all keys still accessible after resize
    for (int i = 0; i < 50; i++) {
        int* result = (int*)hashmap_get(map, &keys[i]);
        if (!result || *result != keys[i]) { FAIL("resize broke retrieval"); hashmap_free(map); return; }
    }

    hashmap_free(map);
    PASS();
}

// Test remove
void test_hashmap_remove(void) {
    TEST(hashmap_remove);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    int keys[5];
    for (int i = 0; i < 5; i++) {
        keys[i] = i + 10;
        hashmap_put(map, &keys[i], &keys[i]);
    }

    if (hashmap_size(map) != 5) { FAIL("size should be 5"); hashmap_free(map); return; }

    // Remove middle element
    hashmap_remove(map, &keys[2]);
    if (hashmap_size(map) != 4) { FAIL("size should be 4 after remove"); hashmap_free(map); return; }

    // Verify removed key is gone
    int* result = (int*)hashmap_get(map, &keys[2]);
    if (result != NULL) { FAIL("removed key should return NULL"); hashmap_free(map); return; }

    // Verify other keys still work
    result = (int*)hashmap_get(map, &keys[0]);
    if (!result || *result != keys[0]) { FAIL("other keys should still work"); hashmap_free(map); return; }

    // Remove non-existent key (should be no-op)
    int fake = 999;
    hashmap_remove(map, &fake);
    if (hashmap_size(map) != 4) { FAIL("removing non-existent should be no-op"); hashmap_free(map); return; }

    hashmap_free(map);
    PASS();
}

// Test contains
void test_hashmap_contains(void) {
    TEST(hashmap_contains);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    int key1 = 42;
    int key2 = 99;

    hashmap_put(map, &key1, &key1);

    if (!hashmap_contains(map, &key1)) { FAIL("contains should return true for existing key"); hashmap_free(map); return; }
    if (hashmap_contains(map, &key2)) { FAIL("contains should return false for non-existent key"); hashmap_free(map); return; }

    hashmap_free(map);
    PASS();
}

// Test foreach iteration
static int foreach_count = 0;
static void foreach_callback(void* key, void* value, void* ctx) {
    (void)key;
    (void)value;
    int* count = (int*)ctx;
    (*count)++;
}

void test_hashmap_foreach(void) {
    TEST(hashmap_foreach);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    int keys[10];
    for (int i = 0; i < 10; i++) {
        keys[i] = i;
        hashmap_put(map, &keys[i], &keys[i]);
    }

    foreach_count = 0;
    hashmap_foreach(map, foreach_callback, &foreach_count);

    if (foreach_count != 10) { FAIL("foreach should visit all entries"); hashmap_free(map); return; }

    hashmap_free(map);
    PASS();
}

// Test NULL handling
void test_hashmap_null_inputs(void) {
    TEST(hashmap_null_inputs);

    // These should not crash
    hashmap_free(NULL);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    // NULL key operations
    void* result = hashmap_get(map, NULL);
    (void)result;  // Just checking it doesn't crash

    hashmap_free(map);

    // Operations on NULL map
    result = hashmap_get(NULL, &map);
    if (result != NULL) { FAIL("get on NULL map should return NULL"); return; }

    if (hashmap_size(NULL) != 0) { FAIL("size of NULL map should be 0"); return; }
    if (hashmap_contains(NULL, &map)) { FAIL("contains on NULL map should be false"); return; }

    PASS();
}

// Test many entries (stress test)
void test_hashmap_many_entries(void) {
    TEST(hashmap_many_entries);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    // Insert 1000 entries
    int* keys = malloc(1000 * sizeof(int));
    if (!keys) { FAIL("malloc failed"); hashmap_free(map); return; }

    for (int i = 0; i < 1000; i++) {
        keys[i] = i * 13;  // Spread values
        hashmap_put(map, &keys[i], &keys[i]);
    }

    if (hashmap_size(map) != 1000) { FAIL("size should be 1000"); free(keys); hashmap_free(map); return; }

    // Verify random access
    for (int i = 0; i < 1000; i += 100) {
        int* result = (int*)hashmap_get(map, &keys[i]);
        if (!result || *result != keys[i]) { FAIL("retrieval failed"); free(keys); hashmap_free(map); return; }
    }

    // Remove half
    for (int i = 0; i < 500; i++) {
        hashmap_remove(map, &keys[i]);
    }

    if (hashmap_size(map) != 500) { FAIL("size should be 500 after removal"); free(keys); hashmap_free(map); return; }

    free(keys);
    hashmap_free(map);
    PASS();
}

// Test clear
void test_hashmap_clear(void) {
    TEST(hashmap_clear);

    HashMap* map = hashmap_new();
    if (!map) { FAIL("hashmap_new returned NULL"); return; }

    int keys[10];
    for (int i = 0; i < 10; i++) {
        keys[i] = i;
        hashmap_put(map, &keys[i], &keys[i]);
    }

    hashmap_clear(map);
    if (hashmap_size(map) != 0) { FAIL("size should be 0 after clear"); hashmap_free(map); return; }

    // Verify keys are gone
    int* result = (int*)hashmap_get(map, &keys[0]);
    if (result != NULL) { FAIL("keys should be gone after clear"); hashmap_free(map); return; }

    // Can still add after clear
    hashmap_put(map, &keys[0], &keys[0]);
    if (hashmap_size(map) != 1) { FAIL("should be able to add after clear"); hashmap_free(map); return; }

    hashmap_free(map);
    PASS();
}

int main(void) {
    printf("Running HashMap Unit Tests...\n\n");

    test_hashmap_create_destroy();
    test_hashmap_put_get();
    test_hashmap_collision();
    test_hashmap_resize();
    test_hashmap_remove();
    test_hashmap_contains();
    test_hashmap_foreach();
    test_hashmap_null_inputs();
    test_hashmap_many_entries();
    test_hashmap_clear();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
