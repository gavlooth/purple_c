// Unit tests for bug fixes
// Tests memory safety improvements: malloc/realloc checks, integer overflow, etc.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>

#include "util/dstring.h"
#include "util/hashmap.h"
#include "types.h"

// Test 1: DString handles NULL gracefully
static void test_dstring_null_safety(void) {
    printf("  test_dstring_null_safety... ");

    // These should not crash
    ds_free(NULL);
    ds_append(NULL, "test");
    ds_append_char(NULL, 'x');
    ds_printf(NULL, "test %d", 42);

    // ds_cstr returns empty string for NULL
    const char* s = ds_cstr(NULL);
    assert(strcmp(s, "") == 0);

    // ds_len returns 0 for NULL
    assert(ds_len(NULL) == 0);

    // ds_ensure_capacity returns 0 for NULL
    assert(ds_ensure_capacity(NULL, 100) == 0);

    printf("PASS\n");
}

// Test 2: DString ensure_capacity returns success/failure
static void test_dstring_capacity_return(void) {
    printf("  test_dstring_capacity_return... ");

    DString* ds = ds_new();
    assert(ds != NULL);

    // Normal capacity increase should succeed
    int result = ds_ensure_capacity(ds, 1000);
    assert(result == 1);
    assert(ds->capacity >= 1000);

    // Already have enough capacity - should succeed
    result = ds_ensure_capacity(ds, 500);
    assert(result == 1);

    ds_free(ds);
    printf("PASS\n");
}

// Test 3: HashMap handles NULL gracefully
static void test_hashmap_null_safety(void) {
    printf("  test_hashmap_null_safety... ");

    // These should not crash
    hashmap_free(NULL);
    hashmap_put(NULL, (void*)1, (void*)2);
    assert(hashmap_get(NULL, (void*)1) == NULL);
    assert(hashmap_remove(NULL, (void*)1) == NULL);
    assert(hashmap_contains(NULL, (void*)1) == 0);
    assert(hashmap_size(NULL) == 0);

    printf("PASS\n");
}

// Test 4: Value constructors handle allocation failure gracefully
static void test_value_null_checks(void) {
    printf("  test_value_null_checks... ");

    // Test that mk_sym handles NULL string
    Value* v = mk_sym(NULL);
    assert(v != NULL);  // Should still create value

    // Test that mk_code handles NULL string
    v = mk_code(NULL);
    assert(v != NULL);

    printf("PASS\n");
}

// Test 5: DString append operations check capacity
static void test_dstring_append_safety(void) {
    printf("  test_dstring_append_safety... ");

    DString* ds = ds_new();
    assert(ds != NULL);

    // Append a lot of data
    const char* test_str = "test string with some content ";
    size_t test_len = strlen(test_str);
    for (int i = 0; i < 1000; i++) {
        ds_append(ds, test_str);
    }

    // Should have grown appropriately
    assert(ds->len == test_len * 1000);
    assert(ds->capacity > ds->len);

    // Verify null termination
    assert(ds->data[ds->len] == '\0');

    ds_free(ds);
    printf("PASS\n");
}

// Test 6: HashMap resize works correctly
static void test_hashmap_resize(void) {
    printf("  test_hashmap_resize... ");

    HashMap* map = hashmap_new();
    assert(map != NULL);

    size_t initial_buckets = map->bucket_count;

    // Add many entries to trigger resize
    for (int i = 0; i < 1000; i++) {
        hashmap_put(map, (void*)(uintptr_t)(i + 1), (void*)(uintptr_t)(i * 2));
    }

    // Should have resized
    assert(map->bucket_count > initial_buckets);
    assert(hashmap_size(map) == 1000);

    // Verify all entries are still accessible
    for (int i = 0; i < 1000; i++) {
        void* val = hashmap_get(map, (void*)(uintptr_t)(i + 1));
        assert(val == (void*)(uintptr_t)(i * 2));
    }

    hashmap_free(map);
    printf("PASS\n");
}

// Test 7: DString printf handles format strings correctly
static void test_dstring_printf(void) {
    printf("  test_dstring_printf... ");

    DString* ds = ds_new();
    assert(ds != NULL);

    ds_printf(ds, "Hello %s, number %d!", "World", 42);
    assert(strcmp(ds_cstr(ds), "Hello World, number 42!") == 0);

    // Append more via printf
    ds_printf(ds, " Extra: %ld", 999999L);
    assert(strstr(ds_cstr(ds), "Extra: 999999") != NULL);

    ds_free(ds);
    printf("PASS\n");
}

// Test 8: DString handles empty strings
static void test_dstring_empty(void) {
    printf("  test_dstring_empty... ");

    DString* ds = ds_new();
    assert(ds != NULL);
    assert(ds_len(ds) == 0);
    assert(strcmp(ds_cstr(ds), "") == 0);

    // Append empty string
    ds_append(ds, "");
    assert(ds_len(ds) == 0);

    // Append something, then clear
    ds_append(ds, "test");
    assert(ds_len(ds) == 4);
    ds_clear(ds);
    assert(ds_len(ds) == 0);
    assert(strcmp(ds_cstr(ds), "") == 0);

    ds_free(ds);
    printf("PASS\n");
}

// Test 9: DString from existing string
static void test_dstring_from(void) {
    printf("  test_dstring_from... ");

    DString* ds = ds_from("Hello World");
    assert(ds != NULL);
    assert(ds_len(ds) == 11);
    assert(strcmp(ds_cstr(ds), "Hello World") == 0);

    // Append to it
    ds_append(ds, "!!!");
    assert(strcmp(ds_cstr(ds), "Hello World!!!") == 0);

    ds_free(ds);

    // Test with NULL
    ds = ds_from(NULL);
    assert(ds != NULL);
    assert(ds_len(ds) == 0);
    ds_free(ds);

    printf("PASS\n");
}

// Test 10: DString take ownership
static void test_dstring_take(void) {
    printf("  test_dstring_take... ");

    DString* ds = ds_new();
    ds_append(ds, "test string");

    char* taken = ds_take(ds);
    assert(taken != NULL);
    assert(strcmp(taken, "test string") == 0);

    // ds is now freed, taken is owned by us
    free(taken);

    // NULL case
    assert(ds_take(NULL) == NULL);

    printf("PASS\n");
}

int main(void) {
    printf("Running Bug Fix Unit Tests...\n");

    test_dstring_null_safety();
    test_dstring_capacity_return();
    test_hashmap_null_safety();
    test_value_null_checks();
    test_dstring_append_safety();
    test_hashmap_resize();
    test_dstring_printf();
    test_dstring_empty();
    test_dstring_from();
    test_dstring_take();

    printf("All bug fix tests passed!\n");
    return 0;
}
