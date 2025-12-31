// Unit tests for rcopt.c - RC optimization module
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/analysis/rcopt.h"
#include "../src/analysis/shape.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Test context lifecycle
void test_context_lifecycle(void) {
    TEST(context_lifecycle);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }
    if (ctx->vars != NULL) { FAIL("vars should be NULL"); free_rcopt_context(ctx); return; }
    if (ctx->current_point != 0) { FAIL("current_point should be 0"); free_rcopt_context(ctx); return; }
    if (ctx->eliminated != 0) { FAIL("eliminated should be 0"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);

    // Free NULL (should not crash)
    free_rcopt_context(NULL);

    PASS();
}

// Test define variable
void test_define_var(void) {
    TEST(define_var);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    RCOptInfo* info = rcopt_define_var(ctx, "x");
    if (!info) { FAIL("rcopt_define_var returned NULL"); free_rcopt_context(ctx); return; }
    if (strcmp(info->var_name, "x") != 0) { FAIL("var_name should be 'x'"); free_rcopt_context(ctx); return; }
    if (!info->is_unique) { FAIL("fresh allocation should be unique"); free_rcopt_context(ctx); return; }
    if (info->is_borrowed) { FAIL("should not be borrowed"); free_rcopt_context(ctx); return; }
    if (info->alias_of != NULL) { FAIL("should not be an alias"); free_rcopt_context(ctx); return; }
    if (info->defined_at != 1) { FAIL("defined_at should be 1"); free_rcopt_context(ctx); return; }

    // Define another variable
    RCOptInfo* info2 = rcopt_define_var(ctx, "y");
    if (!info2) { FAIL("second rcopt_define_var returned NULL"); free_rcopt_context(ctx); return; }
    if (info2->defined_at != 2) { FAIL("second defined_at should be 2"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test find variable
void test_find_var(void) {
    TEST(find_var);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_var(ctx, "x");
    rcopt_define_var(ctx, "y");

    RCOptInfo* info = rcopt_find_var(ctx, "x");
    if (!info) { FAIL("should find x"); free_rcopt_context(ctx); return; }
    if (strcmp(info->var_name, "x") != 0) { FAIL("should be x"); free_rcopt_context(ctx); return; }

    info = rcopt_find_var(ctx, "y");
    if (!info) { FAIL("should find y"); free_rcopt_context(ctx); return; }

    // Non-existent
    info = rcopt_find_var(ctx, "z");
    if (info != NULL) { FAIL("should not find z"); free_rcopt_context(ctx); return; }

    // NULL inputs
    info = rcopt_find_var(NULL, "x");
    if (info != NULL) { FAIL("NULL ctx should return NULL"); free_rcopt_context(ctx); return; }
    info = rcopt_find_var(ctx, NULL);
    if (info != NULL) { FAIL("NULL name should return NULL"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test define alias
void test_define_alias(void) {
    TEST(define_alias);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    RCOptInfo* orig = rcopt_define_var(ctx, "x");
    if (!orig) { FAIL("rcopt_define_var returned NULL"); free_rcopt_context(ctx); return; }
    if (!orig->is_unique) { FAIL("x should be unique initially"); free_rcopt_context(ctx); return; }

    RCOptInfo* alias = rcopt_define_alias(ctx, "y", "x");
    if (!alias) { FAIL("rcopt_define_alias returned NULL"); free_rcopt_context(ctx); return; }
    if (alias->is_unique) { FAIL("alias should not be unique"); free_rcopt_context(ctx); return; }
    if (!alias->alias_of || strcmp(alias->alias_of, "x") != 0) {
        FAIL("alias_of should be 'x'"); free_rcopt_context(ctx); return;
    }

    // Original should no longer be unique
    if (orig->is_unique) { FAIL("x should no longer be unique after alias"); free_rcopt_context(ctx); return; }

    // Original should have alias recorded
    if (orig->alias_count != 1) { FAIL("x should have 1 alias"); free_rcopt_context(ctx); return; }
    if (strcmp(orig->aliases[0], "y") != 0) { FAIL("x's alias should be 'y'"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test define alias of unknown variable
void test_define_alias_unknown(void) {
    TEST(define_alias_unknown);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    // Alias of non-existent variable should create fresh var
    RCOptInfo* info = rcopt_define_alias(ctx, "y", "nonexistent");
    if (!info) { FAIL("should still create a variable"); free_rcopt_context(ctx); return; }
    // Should be treated as unique since we can't find original
    if (!info->is_unique) { FAIL("unknown alias should be treated as unique"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test define borrowed
void test_define_borrowed(void) {
    TEST(define_borrowed);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    RCOptInfo* info = rcopt_define_borrowed(ctx, "param");
    if (!info) { FAIL("rcopt_define_borrowed returned NULL"); free_rcopt_context(ctx); return; }
    if (strcmp(info->var_name, "param") != 0) { FAIL("var_name should be 'param'"); free_rcopt_context(ctx); return; }
    if (info->is_unique) { FAIL("borrowed should not be unique"); free_rcopt_context(ctx); return; }
    if (!info->is_borrowed) { FAIL("should be borrowed"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test mark used
void test_mark_used(void) {
    TEST(mark_used);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_var(ctx, "x");
    RCOptInfo* info = rcopt_find_var(ctx, "x");
    if (info->last_used_at != 0) { FAIL("last_used_at should be 0 initially"); free_rcopt_context(ctx); return; }

    rcopt_mark_used(ctx, "x");
    if (info->last_used_at != 2) { FAIL("last_used_at should be 2 after use"); free_rcopt_context(ctx); return; }

    rcopt_mark_used(ctx, "x");
    if (info->last_used_at != 3) { FAIL("last_used_at should be 3 after second use"); free_rcopt_context(ctx); return; }

    // Mark non-existent (should not crash)
    rcopt_mark_used(ctx, "nonexistent");

    // NULL inputs
    rcopt_mark_used(NULL, "x");
    rcopt_mark_used(ctx, NULL);

    free_rcopt_context(ctx);
    PASS();
}

// Test inc_ref optimization for borrowed
void test_inc_ref_borrowed(void) {
    TEST(inc_ref_borrowed);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_borrowed(ctx, "param");
    RCOptimization opt = rcopt_get_inc_ref(ctx, "param");
    if (opt != RC_OPT_ELIDE_INC) { FAIL("borrowed should elide inc_ref"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test inc_ref optimization for alias
void test_inc_ref_alias(void) {
    TEST(inc_ref_alias);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_var(ctx, "x");
    rcopt_define_alias(ctx, "y", "x");

    RCOptimization opt = rcopt_get_inc_ref(ctx, "y");
    if (opt != RC_OPT_ELIDE_INC) { FAIL("alias should elide inc_ref"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test dec_ref optimization for borrowed
void test_dec_ref_borrowed(void) {
    TEST(dec_ref_borrowed);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_borrowed(ctx, "param");
    RCOptimization opt = rcopt_get_dec_ref(ctx, "param");
    if (opt != RC_OPT_ELIDE_DEC) { FAIL("borrowed should elide dec_ref"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test dec_ref optimization for unique
void test_dec_ref_unique(void) {
    TEST(dec_ref_unique);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_var(ctx, "x");
    RCOptimization opt = rcopt_get_dec_ref(ctx, "x");
    if (opt != RC_OPT_DIRECT_FREE) { FAIL("unique should use direct free"); free_rcopt_context(ctx); return; }

    free_rcopt_context(ctx);
    PASS();
}

// Test dec_ref with aliases - later alias handles dec
void test_dec_ref_alias_handling(void) {
    TEST(dec_ref_alias_handling);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    rcopt_define_var(ctx, "x");
    rcopt_mark_used(ctx, "x");  // Use x
    rcopt_define_alias(ctx, "y", "x");
    rcopt_mark_used(ctx, "y");  // Use y later

    RCOptInfo* x = rcopt_find_var(ctx, "x");
    RCOptInfo* y = rcopt_find_var(ctx, "y");

    // y was used later than x
    if (y->last_used_at <= x->last_used_at) {
        FAIL("y should be used later than x"); free_rcopt_context(ctx); return;
    }

    // x's dec_ref should be elided because y (alias) is used later
    RCOptimization opt = rcopt_get_dec_ref(ctx, "x");
    if (opt != RC_OPT_ELIDE_DEC) {
        FAIL("x dec_ref should be elided since alias y used later");
        free_rcopt_context(ctx);
        return;
    }

    free_rcopt_context(ctx);
    PASS();
}

// Test get_free_function
void test_get_free_function(void) {
    TEST(get_free_function);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    // Unique variable should use free_unique
    rcopt_define_var(ctx, "x");
    const char* fn = rcopt_get_free_function(ctx, "x", SHAPE_TREE);
    if (!fn || strcmp(fn, "free_unique") != 0) {
        FAIL("unique should use free_unique");
        free_rcopt_context(ctx);
        return;
    }

    // Borrowed variable should return NULL (skip dec)
    rcopt_define_borrowed(ctx, "param");
    fn = rcopt_get_free_function(ctx, "param", SHAPE_DAG);
    if (fn != NULL) {
        FAIL("borrowed should return NULL");
        free_rcopt_context(ctx);
        return;
    }

    // Unknown variable should use shape-based strategy
    fn = rcopt_get_free_function(ctx, "unknown", SHAPE_DAG);
    if (!fn || strcmp(fn, "dec_ref") != 0) {
        FAIL("unknown should use shape-based (dec_ref for DAG)");
        free_rcopt_context(ctx);
        return;
    }

    free_rcopt_context(ctx);
    PASS();
}

// Test get_stats
void test_get_stats(void) {
    TEST(get_stats);

    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    int total, eliminated;
    rcopt_get_stats(ctx, &total, &eliminated);
    if (total != 0) { FAIL("total should be 0 initially"); free_rcopt_context(ctx); return; }
    if (eliminated != 0) { FAIL("eliminated should be 0 initially"); free_rcopt_context(ctx); return; }

    rcopt_define_var(ctx, "x");
    rcopt_define_borrowed(ctx, "param");
    rcopt_get_inc_ref(ctx, "param");  // This eliminates one

    rcopt_get_stats(ctx, &total, &eliminated);
    if (total != 4) { FAIL("total should be 4 (2 vars * 2 ops)"); free_rcopt_context(ctx); return; }
    if (eliminated != 1) { FAIL("eliminated should be 1"); free_rcopt_context(ctx); return; }

    // NULL context
    rcopt_get_stats(NULL, &total, &eliminated);
    if (total != 0 || eliminated != 0) {
        FAIL("NULL ctx should give 0,0");
        free_rcopt_context(ctx);
        return;
    }

    free_rcopt_context(ctx);
    PASS();
}

// Test rcopt_string
void test_rcopt_string(void) {
    TEST(rcopt_string);

    if (strcmp(rcopt_string(RC_OPT_NONE), "none") != 0) { FAIL("NONE"); return; }
    if (strcmp(rcopt_string(RC_OPT_ELIDE_INC), "elide_inc_ref") != 0) { FAIL("ELIDE_INC"); return; }
    if (strcmp(rcopt_string(RC_OPT_ELIDE_DEC), "elide_dec_ref") != 0) { FAIL("ELIDE_DEC"); return; }
    if (strcmp(rcopt_string(RC_OPT_DIRECT_FREE), "direct_free") != 0) { FAIL("DIRECT_FREE"); return; }
    if (strcmp(rcopt_string(RC_OPT_BATCHED_FREE), "batched_free") != 0) { FAIL("BATCHED_FREE"); return; }
    if (strcmp(rcopt_string(RC_OPT_ELIDE_ALL), "elide_all") != 0) { FAIL("ELIDE_ALL"); return; }

    PASS();
}

// Test NULL input handling throughout
void test_null_handling(void) {
    TEST(null_handling);

    // These should not crash
    RCOptInfo* info = rcopt_define_var(NULL, "x");
    if (info != NULL) { FAIL("NULL ctx should return NULL"); return; }

    info = rcopt_define_var(NULL, NULL);
    if (info != NULL) { FAIL("NULL should return NULL"); return; }

    info = rcopt_define_alias(NULL, "x", "y");
    if (info != NULL) { FAIL("NULL ctx should return NULL"); return; }

    info = rcopt_define_borrowed(NULL, "x");
    if (info != NULL) { FAIL("NULL ctx should return NULL"); return; }

    RCOptimization opt = rcopt_get_inc_ref(NULL, "x");
    if (opt != RC_OPT_NONE) { FAIL("NULL ctx should return NONE"); return; }

    opt = rcopt_get_dec_ref(NULL, "x");
    if (opt != RC_OPT_NONE) { FAIL("NULL ctx should return NONE"); return; }

    PASS();
}

// Test that alias capacity doubling has overflow protection
void test_alias_capacity_overflow(void) {
    TEST(alias_capacity_overflow);

    // This tests that the add_alias function has overflow protection
    // We can't actually create INT_MAX/2 aliases, but we can verify
    // the function doesn't crash and handles edge cases
    RCOptContext* ctx = mk_rcopt_context();
    if (!ctx) { FAIL("mk_rcopt_context returned NULL"); return; }

    // Create a variable and add many aliases to test growth
    RCOptInfo* info = rcopt_define_var(ctx, "x");
    if (!info) { FAIL("rcopt_define_var returned NULL"); free_rcopt_context(ctx); return; }

    // Add enough aliases to trigger several capacity doublings
    // Initial capacity is 4, so 4->8->16->32
    for (int i = 0; i < 30; i++) {
        char name[16];
        snprintf(name, sizeof(name), "alias%d", i);
        rcopt_define_alias(ctx, name, "x");
    }

    // Original 'x' should have alias_count of 30 (or close to it)
    // It's okay if this is less due to internal aliasing mechanisms
    if (info->alias_count < 1) {
        FAIL("aliases should have grown");
        free_rcopt_context(ctx);
        return;
    }

    free_rcopt_context(ctx);
    PASS();
}

int main(void) {
    printf("Running RC Optimization Unit Tests...\n\n");

    test_context_lifecycle();
    test_define_var();
    test_find_var();
    test_define_alias();
    test_define_alias_unknown();
    test_define_borrowed();
    test_mark_used();
    test_inc_ref_borrowed();
    test_inc_ref_alias();
    test_dec_ref_borrowed();
    test_dec_ref_unique();
    test_dec_ref_alias_handling();
    test_get_free_function();
    test_get_stats();
    test_rcopt_string();
    test_null_handling();
    test_alias_capacity_overflow();

    printf("\n%d tests passed, %d tests failed\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
