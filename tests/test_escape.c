#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/types.h"
#include "../src/analysis/escape.h"
#include "../src/eval/eval.h"

// We need to link against:
// src/types.c
// src/analysis/escape.c
// src/eval/eval.c (for SYM_T, NIL etc)

int main() {
    init_syms();
    AnalysisContext* ctx = mk_analysis_ctx();

    printf("=== Test: Escape Analysis Unsoundness ===\n");

    // Case: (let ((a (mk_int 1))) (let ((x (cons a 2))) x))
    // x is returned -> ESCAPE_GLOBAL
    // a is inside x -> Should be ESCAPE_GLOBAL, but will likely be ESCAPE_ARG
    
    Value* sym_a = mk_sym("a");
    Value* sym_x = mk_sym("x");
    Value* val_a = mk_int(1);
    
    // (cons a 2)
    Value* cons_expr = mk_cell(mk_sym("cons"), mk_cell(sym_a, mk_cell(mk_int(2), NIL)));
    
    // Body returns x
    Value* body = sym_x;

    add_var(ctx, "a");
    add_var(ctx, "x");

    // Simulate analysis of (let ((x (cons a 2))) x) where the let-body result escapes globally
    printf("Analyzing: (let ((x (cons a 2))) x) where result is ESCAPE_GLOBAL\n");
    
    // 1. Analyze escape of x (returned)
    analyze_escape(sym_x, ctx, ESCAPE_GLOBAL);
    
    // 2. Analyze escape of (cons a 2) definition for x
    // In h_let_default, it calls: analyze_escape(val_expr, ctx, ESCAPE_NONE) 
    // Wait, let's look at src/eval/eval.c:755
    /*
        analyze_expr(body, ctx);
        analyze_escape(body, ctx, ESCAPE_NONE);
    */
    // The current compiler always starts analyze_escape with ESCAPE_NONE for the body!
    // And it doesn't propagate the body's escape status back to the bindings. 
    
    analyze_escape(cons_expr, ctx, ESCAPE_ARG); // cons always uses ESCAPE_ARG for its sub-exprs

    VarUsage* usage_a = find_var(ctx, "a");
    VarUsage* usage_x = find_var(ctx, "x");

    printf("Var 'x' escape class: %d (Expected: %d ESCAPE_GLOBAL)\n", usage_x->escape_class, ESCAPE_GLOBAL);
    printf("Var 'a' escape class: %d (Expected: %d ESCAPE_GLOBAL, Actual: ?)\n", usage_a->escape_class, ESCAPE_GLOBAL);

    if (usage_a->escape_class < ESCAPE_GLOBAL) {
        printf("FAIL: Escape status did not propagate from container (x) to element (a)!\n");
    } else {
        printf("PASS: Escape status propagated.\n");
    }

    free_analysis_ctx(ctx);
    return 0;
}
