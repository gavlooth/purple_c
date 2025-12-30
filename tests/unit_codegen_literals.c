// Unit tests for codegen literal emission
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/codegen/codegen.h"
#include "../src/types.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static void test_val_to_c_expr_int(void) {
    TEST(val_to_c_expr_int);

    compiler_arena_init();
    Value* v = mk_int(7);
    char* s = val_to_c_expr(v);
    if (!s) { FAIL("val_to_c_expr returned NULL"); compiler_arena_cleanup(); return; }
    if (strcmp(s, "mk_int(7)") != 0) { FAIL("unexpected C literal for int"); free(s); compiler_arena_cleanup(); return; }
    free(s);
    compiler_arena_cleanup();

    PASS();
}

static void test_val_to_c_expr_list(void) {
    TEST(val_to_c_expr_list);

    compiler_arena_init();
    Value* v = mk_cell(mk_int(1), mk_cell(mk_int(2), NULL));
    char* s = val_to_c_expr(v);
    if (!s) { FAIL("val_to_c_expr returned NULL for list"); compiler_arena_cleanup(); return; }
    if (!strstr(s, "mk_pair")) { FAIL("list literal missing mk_pair"); free(s); compiler_arena_cleanup(); return; }
    if (!strstr(s, "mk_int(1)")) { FAIL("list literal missing mk_int(1)"); free(s); compiler_arena_cleanup(); return; }
    if (!strstr(s, "mk_int(2)")) { FAIL("list literal missing mk_int(2)"); free(s); compiler_arena_cleanup(); return; }
    free(s);
    compiler_arena_cleanup();

    PASS();
}

static void test_val_to_c_expr_unsupported(void) {
    TEST(val_to_c_expr_unsupported);

    compiler_arena_init();
    Value* v = mk_sym("x");
    char* s = val_to_c_expr(v);
    if (s != NULL) { FAIL("expected NULL for unsupported literal"); free(s); compiler_arena_cleanup(); return; }
    compiler_arena_cleanup();

    PASS();
}

int main(void) {
    test_val_to_c_expr_int();
    test_val_to_c_expr_list();
    test_val_to_c_expr_unsupported();

    if (tests_failed) {
        fprintf(stderr, "%d tests failed\n", tests_failed);
        return 1;
    }
    printf("%d tests passed\n", tests_passed);
    return 0;
}
