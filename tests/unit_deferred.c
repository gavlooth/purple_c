// Unit tests for deferred.c - targeting 90%+ coverage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/memory/deferred.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Test basic lifecycle
void test_deferred_create_destroy(void) {
    TEST(deferred_create_destroy);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }
    if (ctx->batch_size != 32) { FAIL("batch_size not set"); free_deferred_context(ctx); return; }
    if (ctx->pending != NULL) { FAIL("pending should be NULL"); free_deferred_context(ctx); return; }
    if (ctx->pending_count != 0) { FAIL("pending_count should be 0"); free_deferred_context(ctx); return; }
    if (!ctx->obj_lookup) { FAIL("obj_lookup should be created"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);

    // Test with zero batch_size (should default to 32)
    DeferredContext* ctx2 = mk_deferred_context(0);
    if (!ctx2) { FAIL("mk_deferred_context(0) returned NULL"); return; }
    if (ctx2->batch_size != 32) { FAIL("default batch_size should be 32"); free_deferred_context(ctx2); return; }
    free_deferred_context(ctx2);

    // Test free NULL (should not crash)
    free_deferred_context(NULL);

    PASS();
}

// Test single defer
void test_defer_single(void) {
    TEST(defer_single);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int obj = 42;
    defer_decrement(ctx, &obj);

    if (ctx->pending_count != 1) { FAIL("pending_count should be 1"); free_deferred_context(ctx); return; }
    if (ctx->pending == NULL) { FAIL("pending should not be NULL"); free_deferred_context(ctx); return; }
    if (ctx->pending->count != 1) { FAIL("count should be 1"); free_deferred_context(ctx); return; }
    if (ctx->total_deferred != 1) { FAIL("total_deferred should be 1"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test multiple defers for same object (coalescing)
void test_defer_coalesce(void) {
    TEST(defer_coalesce);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int obj = 42;
    defer_decrement(ctx, &obj);
    defer_decrement(ctx, &obj);
    defer_decrement(ctx, &obj);

    // Should coalesce into one entry with count 3
    if (ctx->pending_count != 1) { FAIL("pending_count should be 1"); free_deferred_context(ctx); return; }
    if (ctx->pending->count != 3) { FAIL("count should be 3"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test multiple different objects
void test_defer_multiple_objects(void) {
    TEST(defer_multiple_objects);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int objs[5];
    for (int i = 0; i < 5; i++) {
        objs[i] = i;
        defer_decrement(ctx, &objs[i]);
    }

    if (ctx->pending_count != 5) { FAIL("pending_count should be 5"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test process_deferred
void test_process_deferred(void) {
    TEST(process_deferred);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int obj = 42;
    defer_decrement(ctx, &obj);
    defer_decrement(ctx, &obj);  // count = 2

    // Process 1
    process_deferred(ctx, 1);
    if (ctx->pending_count != 1) { FAIL("should still have 1 pending"); free_deferred_context(ctx); return; }
    if (ctx->pending->count != 1) { FAIL("count should be 1"); free_deferred_context(ctx); return; }

    // Process 1 more - should remove
    process_deferred(ctx, 1);
    if (ctx->pending_count != 0) { FAIL("should have 0 pending"); free_deferred_context(ctx); return; }
    if (ctx->pending != NULL) { FAIL("pending should be NULL"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test process multiple objects
void test_process_multiple(void) {
    TEST(process_multiple);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int objs[3];
    for (int i = 0; i < 3; i++) {
        objs[i] = i;
        defer_decrement(ctx, &objs[i]);
    }

    if (ctx->pending_count != 3) { FAIL("should have 3 pending"); free_deferred_context(ctx); return; }

    // Process 2
    process_deferred(ctx, 2);
    if (ctx->pending_count != 1) { FAIL("should have 1 pending"); free_deferred_context(ctx); return; }

    // Process rest
    process_deferred(ctx, 10);
    if (ctx->pending_count != 0) { FAIL("should have 0 pending"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test flush_deferred
void test_flush_deferred(void) {
    TEST(flush_deferred);

    DeferredContext* ctx = mk_deferred_context(8);  // Small batch
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    // Add many deferrals
    int objs[20];
    for (int i = 0; i < 20; i++) {
        objs[i] = i;
        defer_decrement(ctx, &objs[i]);
    }

    if (ctx->pending_count != 20) { FAIL("should have 20 pending"); free_deferred_context(ctx); return; }

    // Flush all
    flush_deferred(ctx);

    if (ctx->pending_count != 0) { FAIL("should have 0 pending after flush"); free_deferred_context(ctx); return; }
    if (ctx->pending != NULL) { FAIL("pending should be NULL after flush"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test should_process_deferred
void test_should_process(void) {
    TEST(should_process);

    DeferredContext* ctx = mk_deferred_context(5);  // Small batch
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int objs[10];

    // Add 4 (below threshold)
    for (int i = 0; i < 4; i++) {
        objs[i] = i;
        defer_decrement(ctx, &objs[i]);
    }

    if (should_process_deferred(ctx)) { FAIL("should not process yet"); free_deferred_context(ctx); return; }

    // Add 1 more (at threshold)
    objs[4] = 4;
    defer_decrement(ctx, &objs[4]);

    if (!should_process_deferred(ctx)) { FAIL("should process now"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test NULL inputs
void test_deferred_null_inputs(void) {
    TEST(deferred_null_inputs);

    // defer_decrement with NULL ctx
    int obj = 42;
    defer_decrement(NULL, &obj);  // Should not crash

    // defer_decrement with NULL obj
    DeferredContext* ctx = mk_deferred_context(32);
    defer_decrement(ctx, NULL);  // Should not crash
    if (ctx->pending_count != 0) { FAIL("should not add NULL obj"); free_deferred_context(ctx); return; }

    // process_deferred with NULL ctx
    process_deferred(NULL, 10);  // Should not crash

    // process_deferred with empty pending
    process_deferred(ctx, 10);  // Should not crash (empty list)

    // should_process with NULL
    if (should_process_deferred(NULL) != 0) { FAIL("NULL ctx should return 0"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test hash collision behavior
void test_deferred_hash_collision(void) {
    TEST(deferred_hash_collision);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    // Add many objects to force hash collisions
    int objs[100];
    for (int i = 0; i < 100; i++) {
        objs[i] = i;
        defer_decrement(ctx, &objs[i]);
    }

    if (ctx->pending_count != 100) { FAIL("should have 100 pending"); free_deferred_context(ctx); return; }

    // Verify coalescing still works with many objects
    for (int i = 0; i < 100; i++) {
        defer_decrement(ctx, &objs[i]);
    }

    // Count should still be 100 (coalesced)
    if (ctx->pending_count != 100) { FAIL("count should still be 100"); free_deferred_context(ctx); return; }

    flush_deferred(ctx);
    flush_deferred(ctx);  // Flush twice to clear count=2 entries

    if (ctx->pending_count != 0) { FAIL("should be empty after flush"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test mixed operations
void test_deferred_mixed_ops(void) {
    TEST(deferred_mixed_ops);

    DeferredContext* ctx = mk_deferred_context(4);  // Very small batch
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int objs[10];
    for (int i = 0; i < 10; i++) objs[i] = i;

    // Add some
    defer_decrement(ctx, &objs[0]);
    defer_decrement(ctx, &objs[1]);
    defer_decrement(ctx, &objs[2]);

    // Process some
    process_deferred(ctx, 2);

    // Add more
    defer_decrement(ctx, &objs[3]);
    defer_decrement(ctx, &objs[4]);

    // Process all
    flush_deferred(ctx);

    if (ctx->pending_count != 0) { FAIL("should be empty"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

// Test statistics tracking
void test_deferred_stats(void) {
    TEST(deferred_stats);

    DeferredContext* ctx = mk_deferred_context(32);
    if (!ctx) { FAIL("mk_deferred_context returned NULL"); return; }

    int objs[5];
    for (int i = 0; i < 5; i++) {
        objs[i] = i;
        defer_decrement(ctx, &objs[i]);
    }

    if (ctx->total_deferred != 5) { FAIL("total_deferred should be 5"); free_deferred_context(ctx); return; }

    // Coalescing shouldn't increase total_deferred
    defer_decrement(ctx, &objs[0]);
    if (ctx->total_deferred != 5) { FAIL("total_deferred should still be 5"); free_deferred_context(ctx); return; }

    free_deferred_context(ctx);
    PASS();
}

int main(void) {
    printf("Running Deferred RC Unit Tests...\n\n");

    test_deferred_create_destroy();
    test_defer_single();
    test_defer_coalesce();
    test_defer_multiple_objects();
    test_process_deferred();
    test_process_multiple();
    test_flush_deferred();
    test_should_process();
    test_deferred_null_inputs();
    test_deferred_hash_collision();
    test_deferred_mixed_ops();
    test_deferred_stats();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
