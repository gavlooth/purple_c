// Unit tests for symmetric.c - Symmetric Reference Counting module
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/memory/symmetric.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Destructor for test data
static void test_destructor(void* data) {
    free(data);
}

// Test object creation and destruction
void test_sym_obj_lifecycle(void) {
    TEST(sym_obj_lifecycle);

    int* data = malloc(sizeof(int));
    if (!data) { FAIL("malloc failed"); return; }
    *data = 42;

    SymObj* obj = sym_obj_new(data, test_destructor);
    if (!obj) { FAIL("sym_obj_new returned NULL"); free(data); return; }
    if (obj->external_rc != 0) { FAIL("external_rc should be 0"); return; }
    if (obj->internal_rc != 0) { FAIL("internal_rc should be 0"); return; }
    if (obj->ref_count != 0) { FAIL("ref_count should be 0"); return; }
    if (obj->freed != 0) { FAIL("freed should be 0"); return; }
    if (obj->data != data) { FAIL("data should match"); return; }

    // Object is not owned, so we need to manually free it
    // First mark as freed to prevent double-free
    obj->freed = 1;
    free(data);
    free(obj->refs);
    free(obj);

    PASS();
}

// Test scope lifecycle
void test_sym_scope_lifecycle(void) {
    TEST(sym_scope_lifecycle);

    SymScope* scope = sym_scope_new(NULL);
    if (!scope) { FAIL("sym_scope_new returned NULL"); return; }
    if (scope->owned_count != 0) { FAIL("owned_count should be 0"); return; }
    if (scope->parent != NULL) { FAIL("parent should be NULL"); return; }

    sym_scope_free(scope);
    // Free NULL should not crash
    sym_scope_free(NULL);

    PASS();
}

// Test context lifecycle
void test_sym_context_lifecycle(void) {
    TEST(sym_context_lifecycle);

    SymContext* ctx = sym_context_new();
    if (!ctx) { FAIL("sym_context_new returned NULL"); return; }
    if (!ctx->global_scope) { FAIL("global_scope should not be NULL"); sym_context_free(ctx); return; }
    if (ctx->stack_size != 1) { FAIL("stack_size should be 1"); sym_context_free(ctx); return; }
    if (ctx->objects_created != 0) { FAIL("objects_created should be 0"); sym_context_free(ctx); return; }

    sym_context_free(ctx);
    // Free NULL should not crash
    sym_context_free(NULL);

    PASS();
}

// Test adding refs with capacity growth (tests overflow protection)
void test_ref_capacity_growth(void) {
    TEST(ref_capacity_growth);

    int* data = malloc(sizeof(int));
    if (!data) { FAIL("malloc failed"); return; }
    *data = 1;

    SymObj* obj = sym_obj_new(data, test_destructor);
    if (!obj) { FAIL("sym_obj_new returned NULL"); free(data); return; }

    // Create target objects
    SymObj* targets[20];
    for (int i = 0; i < 20; i++) {
        int* tdata = malloc(sizeof(int));
        if (!tdata) { FAIL("target malloc failed"); return; }
        *tdata = i;
        targets[i] = sym_obj_new(tdata, test_destructor);
        if (!targets[i]) { FAIL("target sym_obj_new failed"); return; }
    }

    // Add refs - should trigger capacity growth (initial=8, then 16, 32)
    for (int i = 0; i < 20; i++) {
        sym_obj_add_ref(obj, targets[i]);
    }

    if (obj->ref_count != 20) { FAIL("ref_count should be 20"); return; }
    if (obj->ref_capacity < 20) { FAIL("ref_capacity should be >= 20"); return; }

    // Cleanup
    for (int i = 0; i < 20; i++) {
        targets[i]->freed = 1;
        free(targets[i]->data);
        free(targets[i]->refs);
        free(targets[i]);
    }
    obj->freed = 1;
    free(data);
    free(obj->refs);
    free(obj);

    PASS();
}

// Test scope owns capacity growth
void test_scope_owns_capacity_growth(void) {
    TEST(scope_owns_capacity_growth);

    SymScope* scope = sym_scope_new(NULL);
    if (!scope) { FAIL("sym_scope_new returned NULL"); return; }

    // Create and add many objects - should trigger capacity growth
    for (int i = 0; i < 20; i++) {
        int* data = malloc(sizeof(int));
        if (!data) { FAIL("malloc failed"); return; }
        *data = i;
        SymObj* obj = sym_obj_new(data, test_destructor);
        if (!obj) { FAIL("sym_obj_new failed"); free(data); return; }
        sym_scope_own(scope, obj);
    }

    if (scope->owned_count != 20) { FAIL("owned_count should be 20"); sym_scope_free(scope); return; }
    if (scope->owned_capacity < 20) { FAIL("owned_capacity should be >= 20"); sym_scope_free(scope); return; }

    // Cleanup - release will decrement refs and free objects with external_rc=0
    // Objects are automatically freed by sym_scope_release when external_rc drops to 0
    sym_scope_release(scope);
    sym_scope_free(scope);

    PASS();
}

// Test entering scopes with capacity growth
void test_scope_stack_growth(void) {
    TEST(scope_stack_growth);

    SymContext* ctx = sym_context_new();
    if (!ctx) { FAIL("sym_context_new returned NULL"); return; }

    // Initial capacity is 8, enter 15 scopes to trigger growth
    for (int i = 0; i < 15; i++) {
        SymScope* scope = sym_enter_scope(ctx);
        if (!scope) { FAIL("sym_enter_scope returned NULL"); sym_context_free(ctx); return; }
    }

    // 1 global + 15 entered = 16
    if (ctx->stack_size != 16) { FAIL("stack_size should be 16"); sym_context_free(ctx); return; }
    if (ctx->stack_capacity < 16) { FAIL("stack_capacity should be >= 16"); sym_context_free(ctx); return; }

    sym_context_free(ctx);
    PASS();
}

// Test NULL handling
void test_null_handling(void) {
    TEST(null_handling);

    // These should not crash
    sym_obj_add_ref(NULL, NULL);
    sym_scope_own(NULL, NULL);
    sym_scope_release(NULL);
    sym_inc_external(NULL);
    sym_dec_external(NULL);
    sym_dec_internal(NULL);

    // sym_current_scope with NULL
    SymScope* s = sym_current_scope(NULL);
    if (s != NULL) { FAIL("sym_current_scope(NULL) should return NULL"); return; }

    // sym_enter_scope with NULL
    s = sym_enter_scope(NULL);
    if (s != NULL) { FAIL("sym_enter_scope(NULL) should return NULL"); return; }

    // sym_is_orphaned with NULL
    if (!sym_is_orphaned(NULL)) { FAIL("sym_is_orphaned(NULL) should be 1"); return; }

    // sym_total_rc with NULL
    if (sym_total_rc(NULL) != 0) { FAIL("sym_total_rc(NULL) should be 0"); return; }

    PASS();
}

int main(void) {
    printf("Running Symmetric Reference Counting Unit Tests...\n\n");

    test_sym_obj_lifecycle();
    test_sym_scope_lifecycle();
    test_sym_context_lifecycle();
    test_ref_capacity_growth();
    test_scope_owns_capacity_growth();
    test_scope_stack_growth();
    test_null_handling();

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
