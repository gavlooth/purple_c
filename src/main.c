/*
 * ============================================================================
 * Purple C Compiler - ASAP + ISMM 2024 Memory Management
 *
 * Primary Strategy:
 *   - ASAP (As Static As Possible): Compile-time free insertion for acyclic data
 *   - ISMM 2024 (Deeply Immutable Cycles): SCC-based RC for frozen cyclic data
 *   - Deferred RC Fallback: Bounded O(k) processing for mutable cycles
 *
 * Zero-pause guarantee: No stop-the-world GC, O(k) bounded work
 * ============================================================================
 */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>

#include "types.h"
#include "eval/eval.h"
#include "parser/parser.h"
#include "codegen/codegen.h"
#include "memory/scc.h"
#include "memory/deferred.h"
#include "memory/arena.h"
#include "memory/exception.h"
#include "memory/concurrent.h"
#include "analysis/shape.h"
#include "analysis/escape.h"
#include "analysis/dps.h"

// Escape a string for safe use in C single-line comments.
// Returns malloc'd string that caller must free.
// Replaces newlines with \n, tabs with \t, and other control chars with ?.
static char* escape_for_comment(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    // Worst case: every char needs escaping (2 chars each)
    char* out = malloc(len * 2 + 1);
    if (!out) return NULL;
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (c == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (c == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
        } else if (c < 32 || c == 127) {
            out[j++] = '?';
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
    return out;
}

// -- Main Entry Point --

int main(int argc, char** argv) {
    // Initialize compiler arena (Phase 12)
    // All Value* allocations during compilation use this arena
    compiler_arena_init();

    // Initialize symbol table
    init_syms();

    // Initialize type registry (Phase 8)
    init_type_registry();

    // Initial environment with primitives
    Value* env = NIL;

    // Constants
    env = env_extend(env, mk_sym("t"), SYM_T);
    env = env_extend(env, mk_sym("nil"), NIL);

    // Arithmetic
    env = env_extend(env, mk_sym("+"), mk_prim(prim_add));
    env = env_extend(env, mk_sym("-"), mk_prim(prim_sub));
    env = env_extend(env, mk_sym("*"), mk_prim(prim_mul));
    env = env_extend(env, mk_sym("/"), mk_prim(prim_div));
    env = env_extend(env, mk_sym("%"), mk_prim(prim_mod));

    // Comparison
    env = env_extend(env, mk_sym("="), mk_prim(prim_eq));
    env = env_extend(env, mk_sym("<"), mk_prim(prim_lt));
    env = env_extend(env, mk_sym(">"), mk_prim(prim_gt));
    env = env_extend(env, mk_sym("<="), mk_prim(prim_le));
    env = env_extend(env, mk_sym(">="), mk_prim(prim_ge));

    // Logical
    env = env_extend(env, mk_sym("not"), mk_prim(prim_not));

    // List operations
    env = env_extend(env, mk_sym("cons"), mk_prim(prim_cons));
    env = env_extend(env, mk_sym("car"), mk_prim(prim_car));
    env = env_extend(env, mk_sym("cdr"), mk_prim(prim_cdr));
    env = env_extend(env, mk_sym("fst"), mk_prim(prim_fst));
    env = env_extend(env, mk_sym("snd"), mk_prim(prim_snd));
    env = env_extend(env, mk_sym("null?"), mk_prim(prim_null));

    // Other
    env = env_extend(env, mk_sym("run"), mk_prim(prim_run));

    // Initial Meta-Environment (Level 0)
    Value* menv = mk_menv(NIL, env);

    // Generate C runtime header
    gen_runtime_header();

    // Generate weak reference support (Phase 3)
    gen_weak_ref_runtime();

    // Generate Perceus reuse runtime (Phase 4)
    gen_perceus_runtime();

    // Generate SCC runtime (Phase 6b - ISMM 2024)
    gen_scc_runtime();

    // Generate deferred RC runtime (Phase 7)
    gen_deferred_runtime();

    // Generate arena allocator (Phase 8)
    gen_arena_runtime();

    // Generate DPS runtime (Phase 9)
    gen_dps_runtime();

    // Generate exception handling runtime (Phase 10)
    gen_exception_runtime();

    // Generate concurrency runtime (Phase 11)
    gen_concurrent_runtime();

    // Generate ASAP scanner for List type
    gen_asap_scanner("List", 1);

    printf("\n// Runtime arithmetic functions\n");
    printf("Obj* add(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i + b->i); }\n");
    printf("Obj* sub(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i - b->i); }\n");
    printf("Obj* mul(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i * b->i); }\n");
    printf("Obj* div_op(Obj* a, Obj* b) { if (!a || !b || b->i == 0 || (a->i == LONG_MIN && b->i == -1)) return mk_int(0); return mk_int(a->i / b->i); }\n");
    printf("Obj* mod_op(Obj* a, Obj* b) { if (!a || !b || b->i == 0 || (a->i == LONG_MIN && b->i == -1)) return mk_int(0); return mk_int(a->i %% b->i); }\n\n");

    printf("// Runtime comparison functions\n");
    printf("Obj* eq_op(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i == b->i); }\n");
    printf("Obj* lt_op(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i < b->i); }\n");
    printf("Obj* gt_op(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i > b->i); }\n");
    printf("Obj* le_op(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i <= b->i); }\n");
    printf("Obj* ge_op(Obj* a, Obj* b) { if (!a || !b) return mk_int(0); return mk_int(a->i >= b->i); }\n\n");

    printf("// Runtime logical functions\n");
    printf("Obj* not_op(Obj* a, Obj* unused) { (void)unused; if (!a) return mk_int(1); return mk_int(!a->i); }\n\n");

    printf("// Runtime list functions\n");
    printf("int is_nil(Obj* x) { return x == NULL; }\n\n");

    printf("int main() {\n");

    // Process input expressions
    char* input_str = NULL;
    int input_allocated = 0;

    if (argc > 1) {
        // Read from command line argument
        input_str = argv[1];
    } else {
        // Read from stdin using dynamic buffer
        size_t cap = 1024;
        size_t len = 0;
        char* buf = malloc(cap);
        if (!buf) { fprintf(stderr, "OOM\n"); return 1; }
        
        int c;
        while ((c = getchar()) != EOF && c != '\n') {
            if (len + 1 >= cap) {
                if (cap > SIZE_MAX / 2) {
                    fprintf(stderr, "Input too large\n");
                    free(buf);
                    return 1;
                }
                size_t new_cap = cap * 2;
                char* new_buf = realloc(buf, new_cap);
                if (!new_buf) { fprintf(stderr, "OOM\n"); free(buf); return 1; }
                buf = new_buf;
                cap = new_cap;
            }
            buf[len++] = (char)c;
        }
        buf[len] = '\0';
        input_str = buf;
        input_allocated = 1;
    }

    if (input_str && strlen(input_str) > 0) {
        set_parse_input(input_str);
        Value* expr = parse();
        if (expr) {
            Value* result = eval(expr, menv);
            char* str = val_to_str(result);
            if (!str) str = strdup("(error)");
            if (result && result->tag == T_CODE) {
                // Compiled code - output as expression
                char* escaped = escape_for_comment(input_str);
                printf("  // Expression: %s\n", escaped ? escaped : input_str);
                free(escaped);
                printf("  Obj* result = %s;\n", str);
                printf("  if (result) printf(\"Result: %%ld\\n\", result->i);\n");
            } else if (result && result->tag == T_INT) {
                // Interpreted result - output as comment
                printf("  // Result: %ld\n", result->i);
            } else {
                // Other result types
                char* escaped_str = escape_for_comment(str);
                printf("  // Result: %s\n", escaped_str ? escaped_str : str);
                free(escaped_str);
            }
            free(str);
        }
    } else {
        // Default test expression
        const char* test = "(let ((x (lift 10))) (+ x (lift 5)))";
        printf("  // Default test: %s\n", test);
        set_parse_input(test);
        Value* expr = parse();
        if (expr) {
            Value* result = eval(expr, menv);
            char* str = val_to_str(result);
            if (!str) str = strdup("(error)");
            printf("  Obj* result = %s;\n", str);
            printf("  if (result) printf(\"Result: %%ld\\n\", result->i);\n");
            free(str);
        }
    }
    
    if (input_allocated) free(input_str);

    printf("  flush_freelist();\n");
    printf("  flush_all_deferred();\n");
    printf("  cleanup_all_weak_refs();\n");
    printf("  return 0;\n");
    printf("}\n");

    // Cleanup compiler arena - bulk free all Values and strings
    compiler_arena_cleanup();

    return 0;
}
