// Unit tests for shape.c - targeting 90%+ coverage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/analysis/shape.h"

// Local NIL for tests (not using eval.c's global)
static Value nil_value = { .tag = T_NIL };
#define NIL (&nil_value)

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Test shape lattice join
void test_shape_join_lattice(void) {
    TEST(shape_join_lattice);

    // UNKNOWN joins
    if (shape_join(SHAPE_UNKNOWN, SHAPE_UNKNOWN) != SHAPE_UNKNOWN) { FAIL("UNKNOWN+UNKNOWN"); return; }
    if (shape_join(SHAPE_UNKNOWN, SHAPE_TREE) != SHAPE_TREE) { FAIL("UNKNOWN+TREE"); return; }
    if (shape_join(SHAPE_UNKNOWN, SHAPE_DAG) != SHAPE_DAG) { FAIL("UNKNOWN+DAG"); return; }
    if (shape_join(SHAPE_UNKNOWN, SHAPE_CYCLIC) != SHAPE_CYCLIC) { FAIL("UNKNOWN+CYCLIC"); return; }

    // TREE joins
    if (shape_join(SHAPE_TREE, SHAPE_TREE) != SHAPE_TREE) { FAIL("TREE+TREE"); return; }
    if (shape_join(SHAPE_TREE, SHAPE_DAG) != SHAPE_DAG) { FAIL("TREE+DAG"); return; }
    if (shape_join(SHAPE_TREE, SHAPE_CYCLIC) != SHAPE_CYCLIC) { FAIL("TREE+CYCLIC"); return; }

    // DAG joins
    if (shape_join(SHAPE_DAG, SHAPE_DAG) != SHAPE_DAG) { FAIL("DAG+DAG"); return; }
    if (shape_join(SHAPE_DAG, SHAPE_CYCLIC) != SHAPE_CYCLIC) { FAIL("DAG+CYCLIC"); return; }

    // CYCLIC joins
    if (shape_join(SHAPE_CYCLIC, SHAPE_CYCLIC) != SHAPE_CYCLIC) { FAIL("CYCLIC+CYCLIC"); return; }

    // Symmetric
    if (shape_join(SHAPE_DAG, SHAPE_TREE) != SHAPE_DAG) { FAIL("DAG+TREE symmetric"); return; }
    if (shape_join(SHAPE_CYCLIC, SHAPE_TREE) != SHAPE_CYCLIC) { FAIL("CYCLIC+TREE symmetric"); return; }

    PASS();
}

// Test shape_to_string
void test_shape_to_string(void) {
    TEST(shape_to_string);

    if (strcmp(shape_to_string(SHAPE_TREE), "TREE") != 0) { FAIL("TREE string"); return; }
    if (strcmp(shape_to_string(SHAPE_DAG), "DAG") != 0) { FAIL("DAG string"); return; }
    if (strcmp(shape_to_string(SHAPE_CYCLIC), "CYCLIC") != 0) { FAIL("CYCLIC string"); return; }
    if (strcmp(shape_to_string(SHAPE_UNKNOWN), "UNKNOWN") != 0) { FAIL("UNKNOWN string"); return; }

    // Invalid value
    if (strcmp(shape_to_string((Shape)999), "UNKNOWN") != 0) { FAIL("invalid shape string"); return; }

    PASS();
}

// Test context lifecycle
void test_shape_context_lifecycle(void) {
    TEST(shape_context_lifecycle);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }
    if (ctx->shapes != NULL) { FAIL("shapes should be NULL"); return; }
    if (ctx->changed != 0) { FAIL("changed should be 0"); return; }
    if (ctx->next_alias_group != 1) { FAIL("next_alias_group should be 1"); return; }

    // No explicit free function, but can test adding/finding

    PASS();
}

// Test add and find shape
void test_add_find_shape(void) {
    TEST(add_find_shape);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    add_shape(ctx, "x", SHAPE_TREE);
    ShapeInfo* info = find_shape(ctx, "x");
    if (!info) { FAIL("should find x"); return; }
    if (info->shape != SHAPE_TREE) { FAIL("x should be TREE"); return; }

    add_shape(ctx, "y", SHAPE_DAG);
    info = find_shape(ctx, "y");
    if (!info) { FAIL("should find y"); return; }
    if (info->shape != SHAPE_DAG) { FAIL("y should be DAG"); return; }

    // Non-existent
    info = find_shape(ctx, "z");
    if (info != NULL) { FAIL("should not find z"); return; }

    // Update existing (should join)
    add_shape(ctx, "x", SHAPE_DAG);
    info = find_shape(ctx, "x");
    if (info->shape != SHAPE_DAG) { FAIL("x should be DAG after join"); return; }
    if (!ctx->changed) { FAIL("changed should be set"); return; }

    // NULL context
    add_shape(NULL, "a", SHAPE_TREE);  // Should not crash
    info = find_shape(NULL, "a");
    if (info != NULL) { FAIL("NULL context should return NULL"); return; }

    PASS();
}

// Test lookup_shape
void test_lookup_shape(void) {
    TEST(lookup_shape);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    add_shape(ctx, "x", SHAPE_DAG);

    // Lookup symbol
    Value* sym = mk_sym("x");
    Shape s = lookup_shape(ctx, sym);
    if (s != SHAPE_DAG) { FAIL("x should be DAG"); return; }

    // Lookup unknown symbol
    Value* sym2 = mk_sym("unknown");
    s = lookup_shape(ctx, sym2);
    if (s != SHAPE_UNKNOWN) { FAIL("unknown should be UNKNOWN"); return; }

    // Lookup int literal (always TREE)
    Value* num = mk_int(42);
    s = lookup_shape(ctx, num);
    if (s != SHAPE_TREE) { FAIL("int should be TREE"); return; }

    // Lookup nil value (always TREE)
    s = lookup_shape(ctx, NIL);
    if (s != SHAPE_TREE) { FAIL("NIL should be TREE"); return; }

    // NULL inputs
    s = lookup_shape(NULL, sym);
    if (s != SHAPE_UNKNOWN) { FAIL("NULL ctx should return UNKNOWN"); return; }
    s = lookup_shape(ctx, NULL);
    if (s != SHAPE_UNKNOWN) { FAIL("NULL expr should return UNKNOWN"); return; }

    PASS();
}

// Test may_alias
void test_may_alias(void) {
    TEST(may_alias);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // Same symbol definitely aliases
    Value* x1 = mk_sym("x");
    Value* x2 = mk_sym("x");
    if (!may_alias(ctx, x1, x2)) { FAIL("same symbol should alias"); return; }

    // Different literals never alias
    Value* n1 = mk_int(1);
    Value* n2 = mk_int(2);
    if (may_alias(ctx, n1, n2)) { FAIL("different ints should not alias"); return; }

    // NIL never aliases with int
    if (may_alias(ctx, n1, NIL)) { FAIL("int and NIL should not alias"); return; }

    // NULL inputs
    if (may_alias(ctx, NULL, x1)) { FAIL("NULL should not alias"); return; }
    if (may_alias(ctx, x1, NULL)) { FAIL("NULL should not alias"); return; }

    PASS();
}

// Test shape_free_strategy
void test_shape_free_strategy(void) {
    TEST(shape_free_strategy);

    if (strcmp(shape_free_strategy(SHAPE_TREE), "free_tree") != 0) { FAIL("TREE strategy"); return; }
    if (strcmp(shape_free_strategy(SHAPE_DAG), "dec_ref") != 0) { FAIL("DAG strategy"); return; }
    if (strcmp(shape_free_strategy(SHAPE_CYCLIC), "deferred_release") != 0) { FAIL("CYCLIC strategy"); return; }
    if (strcmp(shape_free_strategy(SHAPE_UNKNOWN), "dec_ref") != 0) { FAIL("UNKNOWN strategy"); return; }

    PASS();
}

// Test analyze_shapes_expr with literals
void test_analyze_literals(void) {
    TEST(analyze_literals);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // Int literal
    Value* num = mk_int(42);
    analyze_shapes_expr(num, ctx);
    if (ctx->result_shape != SHAPE_TREE) { FAIL("int should produce TREE"); return; }

    // NIL value
    analyze_shapes_expr(NIL, ctx);
    if (ctx->result_shape != SHAPE_TREE) { FAIL("NIL should produce TREE"); return; }

    // NULL input
    analyze_shapes_expr(NULL, ctx);
    if (ctx->result_shape != SHAPE_TREE) { FAIL("NULL should produce TREE"); return; }

    PASS();
}

// Test analyze_shapes_expr with cons
void test_analyze_cons(void) {
    TEST(analyze_cons);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // cons of two distinct literals -> TREE
    Value* cons_expr = mk_cell(mk_sym("cons"),
                              mk_cell(mk_int(1),
                                     mk_cell(mk_int(2), NIL)));
    analyze_shapes_expr(cons_expr, ctx);
    if (ctx->result_shape != SHAPE_TREE) { FAIL("cons of literals should be TREE"); return; }

    PASS();
}

// Test analyze_shapes_expr with let
void test_analyze_let(void) {
    TEST(analyze_let);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // (let ((x 1)) x)
    Value* let_expr = mk_cell(mk_sym("let"),
                             mk_cell(mk_cell(mk_cell(mk_sym("x"),
                                                    mk_cell(mk_int(1), NIL)),
                                            NIL),
                                    mk_cell(mk_sym("x"), NIL)));
    analyze_shapes_expr(let_expr, ctx);

    // x should be in context as TREE
    ShapeInfo* info = find_shape(ctx, "x");
    if (!info) { FAIL("x should be in context"); return; }
    if (info->shape != SHAPE_TREE) { FAIL("x should be TREE"); return; }

    PASS();
}

// Test analyze_shapes_expr with letrec
void test_analyze_letrec(void) {
    TEST(analyze_letrec);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // (letrec ((x 1)) x) - letrec variables should be CYCLIC
    Value* letrec_expr = mk_cell(mk_sym("letrec"),
                                mk_cell(mk_cell(mk_cell(mk_sym("x"),
                                                       mk_cell(mk_int(1), NIL)),
                                               NIL),
                                       mk_cell(mk_sym("x"), NIL)));
    analyze_shapes_expr(letrec_expr, ctx);

    // x should be CYCLIC
    ShapeInfo* info = find_shape(ctx, "x");
    if (!info) { FAIL("x should be in context"); return; }
    if (info->shape != SHAPE_CYCLIC) { FAIL("letrec x should be CYCLIC"); return; }

    PASS();
}

// Test analyze_shapes_expr with set!
void test_analyze_set(void) {
    TEST(analyze_set);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    add_shape(ctx, "x", SHAPE_TREE);

    // (set! x 1)
    Value* set_expr = mk_cell(mk_sym("set!"),
                             mk_cell(mk_sym("x"),
                                    mk_cell(mk_int(1), NIL)));
    analyze_shapes_expr(set_expr, ctx);

    // x should be upgraded to CYCLIC
    ShapeInfo* info = find_shape(ctx, "x");
    if (!info) { FAIL("x should be in context"); return; }
    if (info->shape != SHAPE_CYCLIC) { FAIL("set! x should be CYCLIC"); return; }

    // Result should be CYCLIC
    if (ctx->result_shape != SHAPE_CYCLIC) { FAIL("set! result should be CYCLIC"); return; }

    PASS();
}

// Test analyze_shapes_expr with if
void test_analyze_if(void) {
    TEST(analyze_if);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    add_shape(ctx, "x", SHAPE_TREE);
    add_shape(ctx, "y", SHAPE_DAG);

    // (if 1 x y) - should join TREE and DAG -> DAG
    Value* if_expr = mk_cell(mk_sym("if"),
                            mk_cell(mk_int(1),
                                   mk_cell(mk_sym("x"),
                                          mk_cell(mk_sym("y"), NIL))));
    analyze_shapes_expr(if_expr, ctx);

    if (ctx->result_shape != SHAPE_DAG) { FAIL("if branches should join to DAG"); return; }

    PASS();
}

// Test analyze_shapes_expr with lambda
void test_analyze_lambda(void) {
    TEST(analyze_lambda);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // (lambda (x) x)
    Value* lambda_expr = mk_cell(mk_sym("lambda"),
                                mk_cell(mk_cell(mk_sym("x"), NIL),
                                       mk_cell(mk_sym("x"), NIL)));
    analyze_shapes_expr(lambda_expr, ctx);

    // Lambda closures are TREE
    if (ctx->result_shape != SHAPE_TREE) { FAIL("lambda should be TREE"); return; }

    PASS();
}

// Test analyze_shapes_expr with lift
void test_analyze_lift(void) {
    TEST(analyze_lift);

    ShapeContext* ctx = mk_shape_context();
    if (!ctx) { FAIL("mk_shape_context returned NULL"); return; }

    // (lift 42) - preserves shape
    Value* lift_expr = mk_cell(mk_sym("lift"),
                              mk_cell(mk_int(42), NIL));
    analyze_shapes_expr(lift_expr, ctx);

    if (ctx->result_shape != SHAPE_TREE) { FAIL("lift of literal should be TREE"); return; }

    PASS();
}

int main(void) {
    printf("Running Shape Analysis Unit Tests...\n\n");

    // Initialize types system for mk_* functions
    compiler_arena_init();

    test_shape_join_lattice();
    test_shape_to_string();
    test_shape_context_lifecycle();
    test_add_find_shape();
    test_lookup_shape();
    test_may_alias();
    test_shape_free_strategy();
    test_analyze_literals();
    test_analyze_cons();
    test_analyze_let();
    test_analyze_letrec();
    test_analyze_set();
    test_analyze_if();
    test_analyze_lambda();
    test_analyze_lift();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    compiler_arena_cleanup();
    return tests_failed > 0 ? 1 : 0;
}
