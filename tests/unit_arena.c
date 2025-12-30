// Unit tests for arena.c - targeting 90%+ coverage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/memory/arena.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Test basic lifecycle
void test_arena_create_destroy(void) {
    TEST(arena_create_destroy);

    Arena* a = arena_create(4096);
    if (!a) { FAIL("arena_create returned NULL"); return; }
    if (a->block_size != 4096) { FAIL("block_size not set"); arena_destroy(a); return; }
    if (a->blocks != NULL) { FAIL("blocks should be NULL initially"); arena_destroy(a); return; }
    if (a->current != NULL) { FAIL("current should be NULL initially"); arena_destroy(a); return; }

    arena_destroy(a);

    // Test with zero block_size (should default to 4096)
    Arena* a2 = arena_create(0);
    if (!a2) { FAIL("arena_create(0) returned NULL"); return; }
    if (a2->block_size != 4096) { FAIL("default block_size should be 4096"); arena_destroy(a2); return; }
    arena_destroy(a2);

    // Test destroy NULL (should not crash)
    arena_destroy(NULL);

    PASS();
}

// Test allocation
void test_arena_alloc(void) {
    TEST(arena_alloc);

    Arena* a = arena_create(1024);
    if (!a) { FAIL("arena_create returned NULL"); return; }

    // Allocate some memory
    void* p1 = arena_alloc(a, 100);
    if (!p1) { FAIL("arena_alloc returned NULL"); arena_destroy(a); return; }

    // Block should be created
    if (!a->blocks) { FAIL("blocks should be non-NULL after alloc"); arena_destroy(a); return; }
    if (!a->current) { FAIL("current should be non-NULL after alloc"); arena_destroy(a); return; }

    // Allocate more
    void* p2 = arena_alloc(a, 200);
    if (!p2) { FAIL("second alloc returned NULL"); arena_destroy(a); return; }

    // Pointers should be different
    if (p1 == p2) { FAIL("allocations should be different"); arena_destroy(a); return; }

    arena_destroy(a);
    PASS();
}

// Test alignment
void test_arena_alignment(void) {
    TEST(arena_alignment);

    Arena* a = arena_create(4096);
    if (!a) { FAIL("arena_create returned NULL"); return; }

    // Allocate odd sizes, check 8-byte alignment
    void* p1 = arena_alloc(a, 1);
    void* p2 = arena_alloc(a, 3);
    void* p3 = arena_alloc(a, 7);

    if (((size_t)p1 & 7) != 0) { FAIL("p1 not 8-byte aligned"); arena_destroy(a); return; }
    if (((size_t)p2 & 7) != 0) { FAIL("p2 not 8-byte aligned"); arena_destroy(a); return; }
    if (((size_t)p3 & 7) != 0) { FAIL("p3 not 8-byte aligned"); arena_destroy(a); return; }

    // Check spacing (should be 8 bytes apart due to alignment)
    if ((char*)p2 - (char*)p1 != 8) { FAIL("p2-p1 should be 8"); arena_destroy(a); return; }
    if ((char*)p3 - (char*)p2 != 8) { FAIL("p3-p2 should be 8"); arena_destroy(a); return; }

    arena_destroy(a);
    PASS();
}

// Test multi-block allocation
void test_arena_multi_block(void) {
    TEST(arena_multi_block);

    Arena* a = arena_create(256);  // Small block size
    if (!a) { FAIL("arena_create returned NULL"); return; }

    // Allocate more than one block's worth
    void* ptrs[20];
    for (int i = 0; i < 20; i++) {
        ptrs[i] = arena_alloc(a, 32);
        if (!ptrs[i]) { FAIL("allocation failed"); arena_destroy(a); return; }
    }

    // Should have multiple blocks
    int block_count = 0;
    ArenaBlock* b = a->blocks;
    while (b) {
        block_count++;
        b = b->next;
    }
    if (block_count < 2) { FAIL("should have multiple blocks"); arena_destroy(a); return; }

    arena_destroy(a);
    PASS();
}

// Test large allocation (exceeds block_size)
void test_arena_large_alloc(void) {
    TEST(arena_large_alloc);

    Arena* a = arena_create(256);  // Small block size
    if (!a) { FAIL("arena_create returned NULL"); return; }

    // Allocate more than block_size
    void* p = arena_alloc(a, 1024);
    if (!p) { FAIL("large alloc returned NULL"); arena_destroy(a); return; }

    // Block should have been sized to fit
    if (a->current->size < 1024) { FAIL("block should be at least 1024"); arena_destroy(a); return; }

    arena_destroy(a);
    PASS();
}

// Test reset
void test_arena_reset(void) {
    TEST(arena_reset);

    Arena* a = arena_create(4096);
    if (!a) { FAIL("arena_create returned NULL"); return; }

    // Allocate some memory
    arena_alloc(a, 100);
    arena_alloc(a, 200);

    size_t used_before = a->current->used;
    if (used_before == 0) { FAIL("should have used memory"); arena_destroy(a); return; }

    arena_reset(a);

    // All blocks should be reset
    ArenaBlock* b = a->blocks;
    while (b) {
        if (b->used != 0) { FAIL("block used should be 0 after reset"); arena_destroy(a); return; }
        b = b->next;
    }

    // Current should point to first block
    if (a->current != a->blocks) { FAIL("current should be first block"); arena_destroy(a); return; }

    // Can allocate again after reset
    void* p = arena_alloc(a, 50);
    if (!p) { FAIL("alloc after reset failed"); arena_destroy(a); return; }

    arena_destroy(a);

    // Reset NULL (should not crash)
    arena_reset(NULL);

    PASS();
}

// Track external cleanup calls
static int external_cleanup_count = 0;
static void* last_cleaned_ptr = NULL;

static void test_cleanup_fn(void* ptr) {
    external_cleanup_count++;
    last_cleaned_ptr = ptr;
    free(ptr);
}

// Test external cleanup registration
void test_arena_external_cleanup(void) {
    TEST(arena_external_cleanup);

    Arena* a = arena_create(4096);
    if (!a) { FAIL("arena_create returned NULL"); return; }

    external_cleanup_count = 0;
    last_cleaned_ptr = NULL;

    // Register some external allocations
    void* ext1 = malloc(100);
    void* ext2 = malloc(200);

    arena_register_external(a, ext1, test_cleanup_fn);
    arena_register_external(a, ext2, test_cleanup_fn);

    // Destroy should call cleanup functions
    arena_destroy(a);

    if (external_cleanup_count != 2) { FAIL("should have called cleanup twice"); return; }

    PASS();
}

// Test release_externals separately
void test_arena_release_externals(void) {
    TEST(arena_release_externals);

    Arena* a = arena_create(4096);
    if (!a) { FAIL("arena_create returned NULL"); return; }

    external_cleanup_count = 0;

    void* ext = malloc(100);
    arena_register_external(a, ext, test_cleanup_fn);

    arena_release_externals(a);

    if (external_cleanup_count != 1) { FAIL("should have called cleanup"); arena_destroy(a); return; }

    // Externals list should be empty
    if (a->externals != NULL) { FAIL("externals should be NULL"); arena_destroy(a); return; }

    // Calling release again should be safe
    arena_release_externals(a);

    arena_destroy(a);

    // Release on NULL (should not crash)
    arena_release_externals(NULL);

    PASS();
}

// Test NULL inputs
void test_arena_null_inputs(void) {
    TEST(arena_null_inputs);

    // alloc on NULL
    void* p = arena_alloc(NULL, 100);
    if (p != NULL) { FAIL("alloc on NULL should return NULL"); return; }

    // register_external with NULL arena
    arena_register_external(NULL, (void*)1, test_cleanup_fn);  // Should not crash

    // register_external with NULL ptr
    Arena* a = arena_create(4096);
    arena_register_external(a, NULL, test_cleanup_fn);  // Should not register

    // register_external with NULL release fn
    arena_register_external(a, (void*)1, NULL);  // Should not register

    if (a->externals != NULL) { FAIL("should not have registered invalid externals"); arena_destroy(a); return; }

    arena_destroy(a);
    PASS();
}

// Test should_use_arena
void test_should_use_arena(void) {
    TEST(should_use_arena);

    // Create a simple scope
    ArenaScope scope;
    scope.id = 42;
    scope.var_count = 2;
    char* vars[] = {"x", "y"};
    scope.allocated_vars = vars;
    scope.next = NULL;

    int result = should_use_arena("x", &scope);
    if (result != 42) { FAIL("should find x in scope"); return; }

    result = should_use_arena("y", &scope);
    if (result != 42) { FAIL("should find y in scope"); return; }

    result = should_use_arena("z", &scope);
    if (result != 0) { FAIL("should not find z in scope"); return; }

    // NULL scopes
    result = should_use_arena("x", NULL);
    if (result != 0) { FAIL("NULL scopes should return 0"); return; }

    PASS();
}

// Test find_arena_scopes (currently returns NULL - placeholder)
void test_find_arena_scopes(void) {
    TEST(find_arena_scopes);

    // Currently just a stub
    ArenaScope* scopes = find_arena_scopes(NULL);
    if (scopes != NULL) { FAIL("stub should return NULL"); return; }

    PASS();
}

// Test many allocations (stress test)
void test_arena_stress(void) {
    TEST(arena_stress);

    Arena* a = arena_create(4096);
    if (!a) { FAIL("arena_create returned NULL"); return; }

    // Many small allocations
    for (int i = 0; i < 1000; i++) {
        void* p = arena_alloc(a, 64);
        if (!p) { FAIL("stress allocation failed"); arena_destroy(a); return; }
    }

    // Count blocks
    int blocks = 0;
    ArenaBlock* b = a->blocks;
    while (b) {
        blocks++;
        b = b->next;
    }

    // Should have allocated multiple blocks
    if (blocks < 10) { FAIL("should have many blocks"); arena_destroy(a); return; }

    arena_destroy(a);
    PASS();
}

int main(void) {
    printf("Running Arena Unit Tests...\n\n");

    test_arena_create_destroy();
    test_arena_alloc();
    test_arena_alignment();
    test_arena_multi_block();
    test_arena_large_alloc();
    test_arena_reset();
    test_arena_external_cleanup();
    test_arena_release_externals();
    test_arena_null_inputs();
    test_should_use_arena();
    test_find_arena_scopes();
    test_arena_stress();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
