// Unit tests for escape.c - targeting 90%+ coverage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/analysis/escape.h"

// Local NIL for tests
static Value nil_value = { .tag = T_NIL };
#define NIL (&nil_value)

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Test context lifecycle
void test_context_lifecycle(void) {
    TEST(context_lifecycle);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }
    if (ctx->vars != NULL) { FAIL("vars should be NULL"); free_analysis_ctx(ctx); return; }
    if (ctx->current_depth != 0) { FAIL("current_depth should be 0"); free_analysis_ctx(ctx); return; }
    if (ctx->in_lambda != 0) { FAIL("in_lambda should be 0"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);

    // Free NULL (should not crash)
    free_analysis_ctx(NULL);

    PASS();
}

// Test add and find var
void test_add_find_var(void) {
    TEST(add_find_var);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "x");
    VarUsage* v = find_var(ctx, "x");
    if (!v) { FAIL("should find x"); free_analysis_ctx(ctx); return; }
    if (strcmp(v->name, "x") != 0) { FAIL("name should be x"); free_analysis_ctx(ctx); return; }
    if (v->use_count != 0) { FAIL("use_count should be 0"); free_analysis_ctx(ctx); return; }
    if (v->escape_class != ESCAPE_NONE) { FAIL("escape_class should be ESCAPE_NONE"); free_analysis_ctx(ctx); return; }

    add_var(ctx, "y");
    v = find_var(ctx, "y");
    if (!v) { FAIL("should find y"); free_analysis_ctx(ctx); return; }

    // Non-existent
    v = find_var(ctx, "z");
    if (v != NULL) { FAIL("should not find z"); free_analysis_ctx(ctx); return; }

    // NULL inputs
    add_var(NULL, "a");  // Should not crash
    add_var(ctx, NULL);  // Should not crash
    v = find_var(NULL, "a");
    if (v != NULL) { FAIL("NULL ctx should return NULL"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test record_use
void test_record_use(void) {
    TEST(record_use);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "x");
    ctx->current_depth = 5;

    record_use(ctx, "x");
    VarUsage* v = find_var(ctx, "x");
    if (v->use_count != 1) { FAIL("use_count should be 1"); free_analysis_ctx(ctx); return; }
    if (v->last_use_depth != 5) { FAIL("last_use_depth should be 5"); free_analysis_ctx(ctx); return; }

    // Record use again
    ctx->current_depth = 7;
    record_use(ctx, "x");
    if (v->use_count != 2) { FAIL("use_count should be 2"); free_analysis_ctx(ctx); return; }
    if (v->last_use_depth != 7) { FAIL("last_use_depth should be 7"); free_analysis_ctx(ctx); return; }

    // Use in lambda marks captured
    ctx->in_lambda = 1;
    record_use(ctx, "x");
    if (!v->captured_by_lambda) { FAIL("should be captured by lambda"); free_analysis_ctx(ctx); return; }

    // Record non-existent var (should be no-op)
    record_use(ctx, "nonexistent");

    // NULL context
    record_use(NULL, "x");  // Should not crash

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_expr with symbol
void test_analyze_expr_symbol(void) {
    TEST(analyze_expr_symbol);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "x");
    ctx->current_depth = 3;

    Value* sym = mk_sym("x");
    analyze_expr(sym, ctx);

    VarUsage* v = find_var(ctx, "x");
    if (v->use_count != 1) { FAIL("use_count should be 1"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_expr with let
void test_analyze_expr_let(void) {
    TEST(analyze_expr_let);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    // (let ((x 1)) x)
    Value* let_expr = mk_cell(mk_sym("let"),
                             mk_cell(mk_cell(mk_cell(mk_sym("y"),
                                                    mk_cell(mk_int(1), NIL)),
                                            NIL),
                                    mk_cell(mk_sym("y"), NIL)));
    add_var(ctx, "y");
    analyze_expr(let_expr, ctx);

    // Depth should have increased and decreased
    if (ctx->current_depth != 0) { FAIL("depth should return to 0"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_expr with lambda
void test_analyze_expr_lambda(void) {
    TEST(analyze_expr_lambda);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "outer");

    // (lambda (x) outer)
    Value* lambda_expr = mk_cell(mk_sym("lambda"),
                                mk_cell(mk_cell(mk_sym("x"), NIL),
                                       mk_cell(mk_sym("outer"), NIL)));
    analyze_expr(lambda_expr, ctx);

    VarUsage* v = find_var(ctx, "outer");
    if (!v->captured_by_lambda) { FAIL("outer should be captured"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_escape with ESCAPE_NONE
void test_analyze_escape_none(void) {
    TEST(analyze_escape_none);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "x");

    Value* sym = mk_sym("x");
    analyze_escape(sym, ctx, ESCAPE_NONE);

    VarUsage* v = find_var(ctx, "x");
    // ESCAPE_NONE is 0, so it won't upgrade from 0
    if (v->escape_class != ESCAPE_NONE) { FAIL("should be ESCAPE_NONE"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_escape with ESCAPE_ARG
void test_analyze_escape_arg(void) {
    TEST(analyze_escape_arg);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "x");

    Value* sym = mk_sym("x");
    analyze_escape(sym, ctx, ESCAPE_ARG);

    VarUsage* v = find_var(ctx, "x");
    if (v->escape_class != ESCAPE_ARG) { FAIL("should be ESCAPE_ARG"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_escape with ESCAPE_GLOBAL
void test_analyze_escape_global(void) {
    TEST(analyze_escape_global);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "x");

    Value* sym = mk_sym("x");
    analyze_escape(sym, ctx, ESCAPE_GLOBAL);

    VarUsage* v = find_var(ctx, "x");
    if (v->escape_class != ESCAPE_GLOBAL) { FAIL("should be ESCAPE_GLOBAL"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_escape with letrec
void test_analyze_escape_letrec(void) {
    TEST(analyze_escape_letrec);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "rec");

    // (letrec ((rec 1)) rec) - letrec should mark as ESCAPE_GLOBAL
    Value* letrec_expr = mk_cell(mk_sym("letrec"),
                                mk_cell(mk_cell(mk_cell(mk_sym("rec"),
                                                       mk_cell(mk_int(1), NIL)),
                                               NIL),
                                       mk_cell(mk_sym("rec"), NIL)));
    analyze_escape(letrec_expr, ctx, ESCAPE_NONE);

    VarUsage* v = find_var(ctx, "rec");
    if (v->escape_class != ESCAPE_GLOBAL) { FAIL("letrec should be ESCAPE_GLOBAL"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_escape with set!
void test_analyze_escape_set(void) {
    TEST(analyze_escape_set);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "mutated");

    // (set! mutated 42) - set! should mark target as ESCAPE_GLOBAL
    Value* set_expr = mk_cell(mk_sym("set!"),
                             mk_cell(mk_sym("mutated"),
                                    mk_cell(mk_int(42), NIL)));
    analyze_escape(set_expr, ctx, ESCAPE_NONE);

    VarUsage* v = find_var(ctx, "mutated");
    if (v->escape_class != ESCAPE_GLOBAL) { FAIL("set! should mark as ESCAPE_GLOBAL"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test analyze_escape with cons
void test_analyze_escape_cons(void) {
    TEST(analyze_escape_cons);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    add_var(ctx, "a");
    add_var(ctx, "b");

    // (cons a b) - args should be ESCAPE_ARG
    Value* cons_expr = mk_cell(mk_sym("cons"),
                              mk_cell(mk_sym("a"),
                                     mk_cell(mk_sym("b"), NIL)));
    analyze_escape(cons_expr, ctx, ESCAPE_NONE);

    VarUsage* va = find_var(ctx, "a");
    VarUsage* vb = find_var(ctx, "b");
    if (va->escape_class != ESCAPE_ARG) { FAIL("a should be ESCAPE_ARG"); free_analysis_ctx(ctx); return; }
    if (vb->escape_class != ESCAPE_ARG) { FAIL("b should be ESCAPE_ARG"); free_analysis_ctx(ctx); return; }

    free_analysis_ctx(ctx);
    PASS();
}

// Test find_free_vars
void test_find_free_vars_simple(void) {
    TEST(find_free_vars_simple);

    char** free_vars = NULL;
    int count = 0;

    // Expression: x (with no bound vars)
    Value* expr = mk_sym("x");
    find_free_vars(expr, NIL, &free_vars, &count);

    if (count != 1) { FAIL("should find 1 free var"); goto cleanup; }
    if (strcmp(free_vars[0], "x") != 0) { FAIL("free var should be x"); goto cleanup; }

    // Clean up
    for (int i = 0; i < count; i++) free(free_vars[i]);
    free(free_vars);
    PASS();
    return;

cleanup:
    for (int i = 0; i < count; i++) free(free_vars[i]);
    free(free_vars);
}

// Test find_free_vars with bound variable
void test_find_free_vars_bound(void) {
    TEST(find_free_vars_bound);

    char** free_vars = NULL;
    int count = 0;

    // Expression: x with x bound
    Value* expr = mk_sym("x");
    Value* bound = mk_cell(mk_cell(mk_sym("x"), mk_int(1)), NIL);
    find_free_vars(expr, bound, &free_vars, &count);

    if (count != 0) { FAIL("should find 0 free vars"); goto cleanup; }

    free(free_vars);
    PASS();
    return;

cleanup:
    for (int i = 0; i < count; i++) free(free_vars[i]);
    free(free_vars);
}

// Test find_free_vars with lambda
void test_find_free_vars_lambda(void) {
    TEST(find_free_vars_lambda);

    char** free_vars = NULL;
    int count = 0;

    // (lambda (x) y) - y is free, x is bound
    Value* lambda_expr = mk_cell(mk_sym("lambda"),
                                mk_cell(mk_cell(mk_sym("x"), NIL),
                                       mk_cell(mk_sym("y"), NIL)));
    find_free_vars(lambda_expr, NIL, &free_vars, &count);

    if (count != 1) { FAIL("should find 1 free var"); goto cleanup; }
    if (strcmp(free_vars[0], "y") != 0) { FAIL("free var should be y"); goto cleanup; }

    for (int i = 0; i < count; i++) free(free_vars[i]);
    free(free_vars);
    PASS();
    return;

cleanup:
    for (int i = 0; i < count; i++) free(free_vars[i]);
    free(free_vars);
}

// Test find_free_vars with quote (should not recurse)
void test_find_free_vars_quote(void) {
    TEST(find_free_vars_quote);

    char** free_vars = NULL;
    int count = 0;

    // (quote x) - x should NOT be free (it's quoted)
    Value* quote_expr = mk_cell(mk_sym("quote"),
                               mk_cell(mk_sym("x"), NIL));
    find_free_vars(quote_expr, NIL, &free_vars, &count);

    if (count != 0) { FAIL("should find 0 free vars in quote"); goto cleanup; }

    free(free_vars);
    PASS();
    return;

cleanup:
    for (int i = 0; i < count; i++) free(free_vars[i]);
    free(free_vars);
}

// Test analyze_escape NULL handling
void test_analyze_escape_null(void) {
    TEST(analyze_escape_null);

    AnalysisContext* ctx = mk_analysis_ctx();
    if (!ctx) { FAIL("mk_analysis_ctx returned NULL"); return; }

    // Should not crash on NULL
    analyze_escape(NULL, ctx, ESCAPE_NONE);
    analyze_escape(NIL, ctx, ESCAPE_NONE);

    free_analysis_ctx(ctx);
    PASS();
}

int main(void) {
    printf("Running Escape Analysis Unit Tests...\n\n");

    // Initialize types system
    compiler_arena_init();

    test_context_lifecycle();
    test_add_find_var();
    test_record_use();
    test_analyze_expr_symbol();
    test_analyze_expr_let();
    test_analyze_expr_lambda();
    test_analyze_escape_none();
    test_analyze_escape_arg();
    test_analyze_escape_global();
    test_analyze_escape_letrec();
    test_analyze_escape_set();
    test_analyze_escape_cons();
    test_find_free_vars_simple();
    test_find_free_vars_bound();
    test_find_free_vars_lambda();
    test_find_free_vars_quote();
    test_analyze_escape_null();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    compiler_arena_cleanup();
    return tests_failed > 0 ? 1 : 0;
}
