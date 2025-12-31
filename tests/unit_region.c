/*
 * Unit tests for Region References (region.c)
 * Tests overflow protection and basic operations
 */

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "../src/memory/region.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing %s... ", name);

#define PASS() \
    do { printf("PASS\n"); tests_passed++; } while(0)

#define FAIL(msg) \
    do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

/* Test basic context creation */
static void test_context_new(void) {
    TEST("context_new");

    RegionContext* ctx = region_context_new();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    if (!ctx->root || !ctx->current) {
        region_context_free(ctx);
        FAIL("Context has NULL root or current");
        return;
    }

    region_context_free(ctx);
    PASS();
}

/* Test region enter/exit */
static void test_region_enter_exit(void) {
    TEST("region_enter_exit");

    RegionContext* ctx = region_context_new();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    Region* root = ctx->current;
    Region* child = region_enter(ctx);
    if (!child) {
        region_context_free(ctx);
        FAIL("Failed to enter child region");
        return;
    }

    if (ctx->current != child) {
        region_context_free(ctx);
        FAIL("Current region not updated after enter");
        return;
    }

    if (child->depth != 1) {
        region_context_free(ctx);
        FAIL("Child depth should be 1");
        return;
    }

    RegionError err = region_exit(ctx);
    if (err != REGION_OK) {
        region_context_free(ctx);
        FAIL("Failed to exit child region");
        return;
    }

    if (ctx->current != root) {
        region_context_free(ctx);
        FAIL("Current not restored to root after exit");
        return;
    }

    region_context_free(ctx);
    PASS();
}

/* Test alloc in region */
static void test_region_alloc(void) {
    TEST("region_alloc");

    RegionContext* ctx = region_context_new();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    int* data = malloc(sizeof(int));
    *data = 42;

    RegionObj* obj = region_alloc(ctx, data, free);
    if (!obj) {
        free(data);
        region_context_free(ctx);
        FAIL("Failed to allocate object");
        return;
    }

    if (obj->data != data) {
        region_context_free(ctx);
        FAIL("Object data not set correctly");
        return;
    }

    region_context_free(ctx);
    PASS();
}

/* Test scope violation detection */
static void test_scope_violation(void) {
    TEST("scope_violation");

    RegionContext* ctx = region_context_new();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    /* Allocate object in root */
    RegionObj* outer_obj = region_alloc(ctx, NULL, NULL);
    if (!outer_obj) {
        region_context_free(ctx);
        FAIL("Failed to allocate outer object");
        return;
    }

    /* Enter child region */
    Region* child = region_enter(ctx);
    if (!child) {
        region_context_free(ctx);
        FAIL("Failed to enter child region");
        return;
    }

    /* Allocate object in child */
    RegionObj* inner_obj = region_alloc(ctx, NULL, NULL);
    if (!inner_obj) {
        region_context_free(ctx);
        FAIL("Failed to allocate inner object");
        return;
    }

    /* Inner -> Outer should succeed */
    RegionRef* ref1 = NULL;
    RegionError err = region_create_ref(ctx, inner_obj, outer_obj, &ref1);
    if (err != REGION_OK) {
        region_context_free(ctx);
        FAIL("Inner to outer reference should succeed");
        return;
    }

    /* Outer -> Inner should fail with scope violation */
    RegionRef* ref2 = NULL;
    err = region_create_ref(ctx, outer_obj, inner_obj, &ref2);
    if (err != REGION_ERR_SCOPE_VIOLATION) {
        region_context_free(ctx);
        FAIL("Outer to inner reference should fail with scope violation");
        return;
    }

    region_context_free(ctx);
    PASS();
}

/* Test that can_reference checks depth correctly */
static void test_can_reference(void) {
    TEST("can_reference");

    RegionContext* ctx = region_context_new();
    if (!ctx) {
        FAIL("Failed to create context");
        return;
    }

    RegionObj* outer = region_alloc(ctx, NULL, NULL);
    region_enter(ctx);
    RegionObj* inner = region_alloc(ctx, NULL, NULL);

    if (!region_can_reference(inner, outer)) {
        region_context_free(ctx);
        FAIL("Inner should be able to reference outer");
        return;
    }

    if (region_can_reference(outer, inner)) {
        region_context_free(ctx);
        FAIL("Outer should NOT be able to reference inner");
        return;
    }

    region_context_free(ctx);
    PASS();
}

int main(void) {
    printf("Running Region Unit Tests...\n\n");

    test_context_new();
    test_region_enter_exit();
    test_region_alloc();
    test_scope_violation();
    test_can_reference();

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
