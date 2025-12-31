#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <errno.h>
#include <limits.h>
#include "util/dstring.h"

/* 
 * ============================================================================ 
 * Purple Strategy Implementation in C (From Scratch) + ASAP Memory Management
 * 
 * 1. Stage Polymorphism (Collapsing Tower):
 *    - `eval` is stage-polymorphic: interprets values, compiles Code. 
 * 
 * 2. ASAP Strategy (Memory Management):
 *    - Implements the SCAN/CLEAN strategy from "As Static As Possible".
 *    - Generates specialized C code for scanning/freeing heap structures.
 *    - Uses a Linear regime approximation: frees values after last use. 
 * 
 * 3. Reflective Tower (Purple):
 *    - Evaluation context is a Meta-Environment (MEnv). 
 *    - MEnv contains: Environment (bindings) and Handlers (semantics). 
 *    - Supported: EM (up), get-meta/set-meta! (modify semantics). 
 * 
 * Target: Generates C code that can be compiled with gcc.
 * ============================================================================ 
 */

// -- Types --

typedef enum { 
    T_INT,      // 42
    T_SYM,      // x, +, lambda
    T_CELL,     // (a . b)
    T_PRIM,     // Native function pointer
    T_LAMBDA,   // (lambda (x) body)
    T_CODE,     // <Code: C_Source_String>
    T_NIL,      // ()
    T_MENV,     // Meta-Environment (The Tower Level)
} Tag;

struct Value;

// Evaluator function signature
typedef struct Value* (*EvalFn)(struct Value* exp, struct Value* menv);

// Primitive function signature (for user-level primitives like +)
typedef struct Value* (*PrimFn)(struct Value* args, struct Value* menv);

// Handler function signature (for meta-level semantics like 'app', 'let')
// Takes: exp, menv, and 'next' (continuation/fallback? kept simple here)
typedef struct Value* (*HandlerFn)(struct Value* exp, struct Value* menv);

typedef struct Value {
    Tag tag;
    union {
        long i;                 // T_INT
        char* s;                // T_SYM, T_CODE
        struct {                // T_CELL
            struct Value* car;
            struct Value* cdr;
        } cell;
        PrimFn prim;            // T_PRIM
        struct {                // T_LAMBDA
            struct Value* params;
            struct Value* body;
            struct Value* env;  // Captured environment (bindings only, usually)
        } lam;
        struct {                // T_MENV
            struct Value* env;      // Variable bindings (Sym -> Val)
            struct Value* parent;   // Parent MEnv (Lazy/Infinite in theory)
            // Handlers table (Fixed slots for simplicity)
            HandlerFn h_app;
            HandlerFn h_let;
            HandlerFn h_if;
            HandlerFn h_lit;
            HandlerFn h_var;
        } menv;
    };
} Value;

// -- ASAP Analysis Infrastructure --

typedef enum {
    ESCAPE_NONE = 0,    // Can stack-allocate
    ESCAPE_ARG = 1,     // Escapes via function argument
    ESCAPE_GLOBAL = 2,  // Escapes to return/global
} EscapeClass;

typedef struct VarUsage {
    char* name;              // Variable name
    int use_count;           // Total references
    int last_use_depth;      // AST depth of last use
    int escape_class;        // EscapeClass value
    int captured_by_lambda;  // 1 if captured by closure
    int freed;               // 1 if already freed
    struct VarUsage* next;   // Linked list
} VarUsage;

typedef struct AnalysisContext {
    VarUsage* vars;          // Variable usage list
    int current_depth;       // AST traversal depth
    int in_lambda;           // Are we inside a lambda body?
} AnalysisContext;

// Global analysis context for current compilation
AnalysisContext* g_analysis_ctx = NULL;

// Forward declarations for helper functions and globals
extern Value* NIL;
int is_nil(Value* v);
Value* car(Value* v);
Value* cdr(Value* v);

// -- Phase 2: Shape Analysis (Tree/DAG/Cyclic) --
// Reference: Ghiya & Hendren - "Is it a Tree, DAG, or Cyclic Graph?"
// This allows us to use the optimal deallocation strategy per variable:
// - TREE: Direct free, no reference counting needed
// - DAG: Reference counting without cycle detection
// - CYCLIC: Arena allocation or local cycle detection

typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_TREE,      // No sharing, no cycles - pure ASAP direct free
    SHAPE_DAG,       // Sharing but no cycles - refcount without cycle check
    SHAPE_CYCLIC     // May have cycles - needs arena or CactusRef
} Shape;

typedef struct ShapeInfo {
    char* var_name;
    Shape shape;
    int confidence;      // 0-100, how certain we are
    int alias_group;     // Variables in same group may alias
    struct ShapeInfo* next;
} ShapeInfo;

typedef struct ShapeContext {
    ShapeInfo* shapes;
    int changed;         // For fixpoint iteration
    int next_alias_group;
    Shape result_shape;  // Shape of last expression result
} ShapeContext;

// Shape lattice: TREE < DAG < CYCLIC (join = max)
Shape shape_join(Shape a, Shape b) {
    if (a == SHAPE_CYCLIC || b == SHAPE_CYCLIC) return SHAPE_CYCLIC;
    if (a == SHAPE_DAG || b == SHAPE_DAG) return SHAPE_DAG;
    if (a == SHAPE_TREE || b == SHAPE_TREE) return SHAPE_TREE;
    return SHAPE_UNKNOWN;
}

ShapeContext* mk_shape_context() {
    ShapeContext* ctx = malloc(sizeof(ShapeContext));
    if (!ctx) return NULL;
    ctx->shapes = NULL;
    ctx->changed = 0;
    ctx->next_alias_group = 1;
    ctx->result_shape = SHAPE_UNKNOWN;
    return ctx;
}

void free_shape_context(ShapeContext* ctx) {
    if (!ctx) return;
    ShapeInfo* s = ctx->shapes;
    while (s) {
        ShapeInfo* next = s->next;
        free(s->var_name);
        free(s);
        s = next;
    }
    free(ctx);
}

ShapeInfo* find_shape(ShapeContext* ctx, const char* name) {
    if (!ctx) return NULL;
    ShapeInfo* s = ctx->shapes;
    while (s) {
        if (strcmp(s->var_name, name) == 0) return s;
        s = s->next;
    }
    return NULL;
}

void add_shape(ShapeContext* ctx, const char* name, Shape shape) {
    if (!ctx) return;
    ShapeInfo* existing = find_shape(ctx, name);
    if (existing) {
        Shape joined = shape_join(existing->shape, shape);
        if (joined != existing->shape) {
            existing->shape = joined;
            ctx->changed = 1;
        }
        return;
    }
    ShapeInfo* s = malloc(sizeof(ShapeInfo));
    if (!s) return;
    s->var_name = strdup(name);
    if (!s->var_name) {
        free(s);
        return;
    }
    s->shape = shape;
    s->confidence = 100;
    s->alias_group = ctx->next_alias_group++;
    s->next = ctx->shapes;
    ctx->shapes = s;
}

Shape lookup_shape(ShapeContext* ctx, Value* expr) {
    if (!ctx || !expr) return SHAPE_UNKNOWN;
    if (expr->tag == T_SYM) {
        ShapeInfo* s = find_shape(ctx, expr->s);
        return s ? s->shape : SHAPE_UNKNOWN;
    }
    // Literals are always TREE (no sharing)
    if (expr->tag == T_INT || expr->tag == T_NIL) {
        return SHAPE_TREE;
    }
    return SHAPE_UNKNOWN;
}

// Check if two expressions may alias
int may_alias(ShapeContext* ctx, Value* a, Value* b) {
    if (!a || !b) return 0;
    // Same variable definitely aliases
    if (a->tag == T_SYM && b->tag == T_SYM && strcmp(a->s, b->s) == 0) {
        return 1;
    }
    // Different literals never alias
    if ((a->tag == T_INT || a->tag == T_NIL) &&
        (b->tag == T_INT || b->tag == T_NIL)) {
        return 0;
    }
    // Check alias groups if both are variables
    if (a->tag == T_SYM && b->tag == T_SYM) {
        ShapeInfo* sa = find_shape(ctx, a->s);
        ShapeInfo* sb = find_shape(ctx, b->s);
        if (sa && sb && sa->alias_group == sb->alias_group) {
            return 1;
        }
    }
    // Conservative: assume may alias
    return 1;
}

// Forward declaration
void analyze_shapes_expr(Value* expr, ShapeContext* ctx);

void analyze_shapes_expr(Value* expr, ShapeContext* ctx) {
    if (!expr || !ctx || is_nil(expr)) {
        ctx->result_shape = SHAPE_TREE;
        return;
    }

    switch (expr->tag) {
        case T_INT:
        case T_NIL:
            ctx->result_shape = SHAPE_TREE;
            break;

        case T_SYM: {
            ShapeInfo* s = find_shape(ctx, expr->s);
            ctx->result_shape = s ? s->shape : SHAPE_UNKNOWN;
            break;
        }

        case T_CELL: {
            Value* op = car(expr);
            Value* args = cdr(expr);

            if (!op) {
                ctx->result_shape = SHAPE_UNKNOWN;
                break;
            }

            if (op->tag == T_SYM) {
                // CONS creates tree structure (unless aliased args)
                if (strcmp(op->s, "cons") == 0) {
                    Value* car_arg = car(args);
                    Value* cdr_arg = car(cdr(args));

                    analyze_shapes_expr(car_arg, ctx);
                    Shape car_shape = ctx->result_shape;

                    analyze_shapes_expr(cdr_arg, ctx);
                    Shape cdr_shape = ctx->result_shape;

                    if (car_shape == SHAPE_TREE && cdr_shape == SHAPE_TREE) {
                        if (!may_alias(ctx, car_arg, cdr_arg)) {
                            ctx->result_shape = SHAPE_TREE;
                        } else {
                            ctx->result_shape = SHAPE_DAG;
                        }
                    } else {
                        ctx->result_shape = shape_join(car_shape, cdr_shape);
                        if (ctx->result_shape == SHAPE_TREE) {
                            ctx->result_shape = SHAPE_DAG;
                        }
                    }
                    return;
                }

                // LET binding
                if (strcmp(op->s, "let") == 0) {
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));

                    while (!is_nil(bindings)) {
                        Value* bind = car(bindings);
                        Value* sym = car(bind);
                        Value* val_expr = car(cdr(bind));

                        analyze_shapes_expr(val_expr, ctx);
                        if (sym && sym->tag == T_SYM) {
                            add_shape(ctx, sym->s, ctx->result_shape);
                        }

                        bindings = cdr(bindings);
                    }

                    analyze_shapes_expr(body, ctx);
                    return;
                }

                // SET! can create cycles
                if (strcmp(op->s, "set!") == 0) {
                    Value* target = car(args);
                    // Setting a field could create a cycle
                    if (target && target->tag == T_SYM) {
                        add_shape(ctx, target->s, SHAPE_CYCLIC);
                    }
                    ctx->result_shape = SHAPE_CYCLIC;
                    return;
                }

                // IF - join both branches
                if (strcmp(op->s, "if") == 0) {
                    Value* cond = car(args);
                    Value* then_br = car(cdr(args));
                    Value* else_br = car(cdr(cdr(args)));

                    analyze_shapes_expr(cond, ctx);
                    analyze_shapes_expr(then_br, ctx);
                    Shape then_shape = ctx->result_shape;
                    analyze_shapes_expr(else_br, ctx);
                    Shape else_shape = ctx->result_shape;

                    ctx->result_shape = shape_join(then_shape, else_shape);
                    return;
                }

                // LAMBDA - captures environment, result is TREE (closure itself)
                if (strcmp(op->s, "lambda") == 0) {
                    ctx->result_shape = SHAPE_TREE;
                    return;
                }

                // LIFT - preserves shape
                if (strcmp(op->s, "lift") == 0) {
                    analyze_shapes_expr(car(args), ctx);
                    return;
                }
            }

            // Default: analyze all subexpressions, result is conservative
            analyze_shapes_expr(op, ctx);
            while (!is_nil(args)) {
                analyze_shapes_expr(car(args), ctx);
                args = cdr(args);
            }
            // Conservative: function calls may return DAG
            if (ctx->result_shape == SHAPE_UNKNOWN) {
                ctx->result_shape = SHAPE_DAG;
            }
            break;
        }

        default:
            ctx->result_shape = SHAPE_UNKNOWN;
            break;
    }
}

// Get shape-based free strategy string
const char* shape_free_strategy(Shape s) {
    switch (s) {
        case SHAPE_TREE: return "free_tree";
        case SHAPE_DAG: return "dec_ref";
        case SHAPE_CYCLIC: return "dec_ref_cyclic";
        default: return "free_obj";
    }
}

// -- Phase 4: Perceus Reuse Analysis --
// Reference: "Perceus: Garbage Free Reference Counting" (Koka)
// Pairs free/alloc of same size for in-place reuse (FBIP)

typedef struct AllocSite {
    int id;
    char* var_name;
    int size;             // Size in bytes (sizeof(Obj) = 1 unit)
    int line_number;
    int reuse_candidate;  // Can this alloc potentially reuse?
    struct AllocSite* next;
} AllocSite;

typedef struct FreeSite {
    int id;
    char* var_name;
    int size;
    int line_number;
    int matched_alloc_id;  // -1 if not matched
    struct FreeSite* next;
} FreeSite;

typedef struct ReusePair {
    FreeSite* free_site;
    AllocSite* alloc_site;
    char* reuse_var;
    struct ReusePair* next;
} ReusePair;

typedef struct ReuseContext {
    AllocSite* allocs;
    FreeSite* frees;
    ReusePair* pairs;
    int next_id;
} ReuseContext;

ReuseContext* mk_reuse_context() {
    ReuseContext* ctx = malloc(sizeof(ReuseContext));
    if (!ctx) return NULL;
    ctx->allocs = NULL;
    ctx->frees = NULL;
    ctx->pairs = NULL;
    ctx->next_id = 1;
    return ctx;
}

void record_alloc(ReuseContext* ctx, const char* var, int size, int line) {
    if (!ctx) return;
    AllocSite* a = malloc(sizeof(AllocSite));
    if (!a) return;
    a->var_name = strdup(var);
    if (!a->var_name) {
        free(a);
        return;
    }
    a->id = ctx->next_id++;
    a->size = size;
    a->line_number = line;
    a->reuse_candidate = 1;
    a->next = ctx->allocs;
    ctx->allocs = a;
}

void record_free(ReuseContext* ctx, const char* var, int size, int line) {
    if (!ctx) return;
    FreeSite* f = malloc(sizeof(FreeSite));
    if (!f) return;
    f->var_name = strdup(var);
    if (!f->var_name) {
        free(f);
        return;
    }
    f->id = ctx->next_id++;
    f->size = size;
    f->line_number = line;
    f->matched_alloc_id = -1;
    f->next = ctx->frees;
    ctx->frees = f;
}

// Check if variable is used between two line numbers
int used_between(ReuseContext* ctx, const char* var, int start, int end) {
    // Simplified: assume not used unless we have more detailed tracking
    // In a real implementation, this would check the usage map
    return 0;
}

// Find reuse opportunities: free followed by alloc of same size
ReusePair* find_reuse_pairs(ReuseContext* ctx) {
    if (!ctx) return NULL;
    ReusePair* pairs = NULL;

    FreeSite* f = ctx->frees;
    while (f) {
        if (f->matched_alloc_id != -1) {
            f = f->next;
            continue;  // Already matched
        }

        AllocSite* a = ctx->allocs;
        while (a) {
            // Must be: free before alloc, same size, not already matched
            if (f->line_number < a->line_number &&
                f->size == a->size &&
                a->reuse_candidate) {

                // Check no intervening use of freed variable
                if (!used_between(ctx, f->var_name, f->line_number, a->line_number)) {
                    // Found a match!
                    ReusePair* p = malloc(sizeof(ReusePair));
                    if (!p) {
                        a = a->next;
                        continue;  // Skip on OOM
                    }
                    p->reuse_var = strdup(f->var_name);
                    if (!p->reuse_var) {
                        free(p);
                        a = a->next;
                        continue;  // Skip on OOM
                    }
                    p->free_site = f;
                    p->alloc_site = a;
                    p->next = pairs;
                    pairs = p;

                    f->matched_alloc_id = a->id;
                    a->reuse_candidate = 0;  // Mark as used
                    break;  // Move to next free
                }
            }
            a = a->next;
        }
        f = f->next;
    }

    ctx->pairs = pairs;
    return pairs;
}

// Generate reuse-aware allocation code
void gen_reuse_alloc(ReusePair* pair, char* buf, int buf_size) {
    snprintf(buf, buf_size,
        "// PERCEUS REUSE: %s -> %s\n"
        "Obj* %s;\n"
        "if (%s != NULL && %s->mark == 1) {\n"
        "    // Reuse in place - FBIP optimization\n"
        "    %s = %s;\n"
        "    %s = NULL;  // Prevent double-free\n"
        "} else {\n"
        "    // Cannot reuse, allocate fresh\n"
        "    if (%s) dec_ref(%s);\n"
        "    %s = malloc(sizeof(Obj));\n"
        "    %s->mark = 1;\n"
        "}\n",
        pair->free_site->var_name, pair->alloc_site->var_name,
        pair->alloc_site->var_name,
        pair->reuse_var, pair->reuse_var,
        pair->alloc_site->var_name, pair->reuse_var,
        pair->reuse_var,
        pair->reuse_var, pair->reuse_var,
        pair->alloc_site->var_name,
        pair->alloc_site->var_name);
}

// -- Phase 5: Non-Lexical Lifetimes (NLL) --
// Reference: Rust Borrow Checker - Free at earliest safe point, not scope end
// This is a simplified CFG-based liveness analysis

typedef struct CFGNode {
    int id;
    Value* expr;              // AST node
    struct CFGNode** succs;   // Successor nodes
    int succ_count;
    struct CFGNode** preds;   // Predecessor nodes
    int pred_count;
    int succ_capacity;
    int pred_capacity;

    // Liveness info (bitset represented as string list for simplicity)
    char** live_in;
    int live_in_count;
    char** live_out;
    int live_out_count;
} CFGNode;

typedef struct CFG {
    CFGNode* entry;
    CFGNode* exit;
    CFGNode** nodes;
    int node_count;
    int node_capacity;
} CFG;

CFGNode* mk_cfg_node(CFG* cfg, Value* expr) {
    if (!cfg) return NULL;
    CFGNode* n = malloc(sizeof(CFGNode));
    if (!n) return NULL;
    n->id = cfg->node_count;
    n->expr = expr;
    n->succs = malloc(4 * sizeof(CFGNode*));
    n->preds = malloc(4 * sizeof(CFGNode*));
    n->succ_count = 0;
    n->pred_count = 0;
    n->succ_capacity = 4;
    n->pred_capacity = 4;
    n->live_in = malloc(32 * sizeof(char*));
    n->live_out = malloc(32 * sizeof(char*));
    n->live_in_count = 0;
    n->live_out_count = 0;

    if (!n->succs || !n->preds || !n->live_in || !n->live_out) {
        free(n->succs);
        free(n->preds);
        free(n->live_in);
        free(n->live_out);
        free(n);
        return NULL;
    }

    if (cfg->node_count >= cfg->node_capacity) {
        if (cfg->node_capacity > INT_MAX / 2) {
            free(n->succs);
            free(n->preds);
            free(n->live_in);
            free(n->live_out);
            free(n);
            return NULL;
        }
        int new_cap = cfg->node_capacity * 2;
        CFGNode** tmp = realloc(cfg->nodes, new_cap * sizeof(CFGNode*));
        if (!tmp) {
            free(n->succs);
            free(n->preds);
            free(n->live_in);
            free(n->live_out);
            free(n);
            return NULL;
        }
        cfg->nodes = tmp;
        cfg->node_capacity = new_cap;
    }
    cfg->nodes[cfg->node_count++] = n;
    return n;
}

void add_cfg_edge(CFGNode* from, CFGNode* to) {
    if (!from || !to) return;
    if (from->succ_count >= from->succ_capacity) {
        if (from->succ_capacity > INT_MAX / 2) return;
        int new_cap = from->succ_capacity * 2;
        CFGNode** tmp = realloc(from->succs, new_cap * sizeof(CFGNode*));
        if (!tmp) return;
        from->succs = tmp;
        from->succ_capacity = new_cap;
    }
    from->succs[from->succ_count++] = to;

    if (to->pred_count >= to->pred_capacity) {
        if (to->pred_capacity > INT_MAX / 2) return;
        int new_cap = to->pred_capacity * 2;
        CFGNode** tmp = realloc(to->preds, new_cap * sizeof(CFGNode*));
        if (!tmp) return;
        to->preds = tmp;
        to->pred_capacity = new_cap;
    }
    to->preds[to->pred_count++] = from;
}

CFG* mk_cfg() {
    CFG* cfg = malloc(sizeof(CFG));
    if (!cfg) return NULL;
    cfg->nodes = malloc(64 * sizeof(CFGNode*));
    if (!cfg->nodes) {
        free(cfg);
        return NULL;
    }
    cfg->node_count = 0;
    cfg->node_capacity = 64;
    cfg->entry = mk_cfg_node(cfg, NULL);
    cfg->exit = mk_cfg_node(cfg, NULL);
    if (!cfg->entry || !cfg->exit) {
        free(cfg->nodes);
        free(cfg);
        return NULL;
    }
    return cfg;
}

// Check if var is in live set
int in_live_set(char** set, int count, const char* var) {
    for (int i = 0; i < count; i++) {
        if (strcmp(set[i], var) == 0) return 1;
    }
    return 0;
}

// Add var to live set
void add_to_live_set(char*** set, int* count, const char* var) {
    if (in_live_set(*set, *count, var)) return;
    char* dup = strdup(var);
    if (!dup) return;  // Skip on OOM
    (*set)[(*count)++] = dup;
}

// Get variables used in an expression
void get_uses(Value* expr, char*** uses, int* count) {
    if (!expr || is_nil(expr)) return;

    if (expr->tag == T_SYM) {
        add_to_live_set(uses, count, expr->s);
        return;
    }

    if (expr->tag == T_CELL) {
        Value* op = car(expr);
        Value* args = cdr(expr);

        if (op->tag == T_SYM && strcmp(op->s, "quote") == 0) return;
        if (op->tag == T_SYM && strcmp(op->s, "lambda") == 0) {
            // Lambda body has different scope
            return;
        }

        get_uses(op, uses, count);
        while (!is_nil(args)) {
            get_uses(car(args), uses, count);
            args = cdr(args);
        }
    }
}

// Compute liveness using dataflow analysis
void compute_liveness(CFG* cfg) {
    int changed = 1;
    int iterations = 0;
    int max_iterations = 100;

    while (changed && iterations < max_iterations) {
        changed = 0;
        iterations++;

        // Iterate in reverse order (from exit to entry)
        for (int i = cfg->node_count - 1; i >= 0; i--) {
            CFGNode* node = cfg->nodes[i];

            // live_out = union of successors' live_in
            char** new_live_out = malloc(64 * sizeof(char*));
            if (!new_live_out) continue;  // Skip node on OOM
            int new_out_count = 0;

            for (int j = 0; j < node->succ_count; j++) {
                CFGNode* succ = node->succs[j];
                for (int k = 0; k < succ->live_in_count; k++) {
                    add_to_live_set(&new_live_out, &new_out_count, succ->live_in[k]);
                }
            }

            // live_in = uses(node) ∪ (live_out - defs(node))
            char** new_live_in = malloc(64 * sizeof(char*));
            if (!new_live_in) {
                // Cleanup new_live_out on failure
                for (int j = 0; j < new_out_count; j++) {
                    free(new_live_out[j]);
                }
                free(new_live_out);
                continue;  // Skip node on OOM
            }
            int new_in_count = 0;

            // Add uses
            if (node->expr) {
                get_uses(node->expr, &new_live_in, &new_in_count);
            }

            // Add live_out
            for (int j = 0; j < new_out_count; j++) {
                add_to_live_set(&new_live_in, &new_in_count, new_live_out[j]);
            }

            // Check for change
            if (new_in_count != node->live_in_count) {
                changed = 1;
            } else {
                for (int j = 0; j < new_in_count; j++) {
                    if (!in_live_set(node->live_in, node->live_in_count, new_live_in[j])) {
                        changed = 1;
                        break;
                    }
                }
            }

            // Update - free old arrays first
            for (int j = 0; j < node->live_in_count; j++) {
                free(node->live_in[j]);
            }
            free(node->live_in);
            for (int j = 0; j < node->live_out_count; j++) {
                free(node->live_out[j]);
            }
            free(node->live_out);
            node->live_in = new_live_in;
            node->live_in_count = new_in_count;
            node->live_out = new_live_out;
            node->live_out_count = new_out_count;
        }
    }
}

// Find where each variable can be freed (earliest point where no longer live)
typedef struct FreePoint {
    char* var_name;
    int node_id;
    int is_conditional;
    struct FreePoint* next;
} FreePoint;

FreePoint* find_free_points(CFG* cfg, const char* var) {
    FreePoint* points = NULL;

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        // Check if var is live_in but not live_out at any successor
        if (in_live_set(node->live_in, node->live_in_count, var)) {
            int dies_on_some_edge = 0;
            int dies_on_all_edges = 1;

            for (int j = 0; j < node->succ_count; j++) {
                if (!in_live_set(node->succs[j]->live_in,
                                 node->succs[j]->live_in_count, var)) {
                    dies_on_some_edge = 1;
                } else {
                    dies_on_all_edges = 0;
                }
            }

            if (dies_on_some_edge) {
                FreePoint* fp = malloc(sizeof(FreePoint));
                if (!fp) continue;  // Skip on OOM
                fp->var_name = strdup(var);
                if (!fp->var_name) {
                    free(fp);
                    continue;  // Skip on OOM
                }
                fp->node_id = node->id;
                fp->is_conditional = !dies_on_all_edges;
                fp->next = points;
                points = fp;
            }
        }
    }

    return points;
}

// Generate NLL comment for free point
void gen_nll_free(FreePoint* fp, char* buf, int buf_size) {
    if (fp->is_conditional) {
        snprintf(buf, buf_size,
            "  // NLL: %s may be freed here on some paths\n"
            "  if (!_path_uses_%s) %s(%s);\n",
            fp->var_name, fp->var_name, "free_obj", fp->var_name);
    } else {
        snprintf(buf, buf_size,
            "  // NLL: %s freed early (before scope end)\n"
            "  %s(%s);\n",
            fp->var_name, "free_obj", fp->var_name);
    }
}

// Generate Perceus runtime helpers
void gen_perceus_runtime() {
    printf("// Phase 4: Perceus Reuse Analysis Runtime\n");
    printf("// Reference: Koka's FBIP (Functional But In-Place) optimization\n\n");

    printf("// Try to reuse an object if it's unique (refcount == 1)\n");
    printf("Obj* try_reuse(Obj* old, size_t size) {\n");
    printf("    if (old && old->mark == 1) {\n");
    printf("        // Reusing: release children if this was a pair\n");
    printf("        if (old->is_pair) {\n");
    printf("            if (old->a) dec_ref(old->a);\n");
    printf("            if (old->b) dec_ref(old->b);\n");
    printf("            old->a = NULL;\n");
    printf("            old->b = NULL;\n");
    printf("        }\n");
    printf("        return old;\n");
    printf("    }\n");
    printf("    if (old) dec_ref(old);\n");
    printf("    return malloc(size);\n");
    printf("}\n\n");

    printf("// Reuse-aware constructors\n");
    printf("Obj* reuse_as_int(Obj* old, long value) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    if (!obj) return NULL;\n");
    printf("    obj->mark = 1;\n");
    printf("    obj->scc_id = -1;\n");
    printf("    obj->is_pair = 0;\n");
    printf("    obj->scan_tag = 0;\n");
    printf("    obj->i = value;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    if (!obj) return NULL;\n");
    printf("    obj->mark = 1;\n");
    printf("    obj->scc_id = -1;\n");
    printf("    obj->is_pair = 1;\n");
    printf("    obj->scan_tag = 0;\n");
    printf("    obj->a = a;\n");
    printf("    obj->b = b;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("// Conditional reuse for branching code\n");
    printf("Obj* conditional_reuse(int cond, Obj* if_reuse, Obj* else_reuse, long value) {\n");
    printf("    if (cond) {\n");
    printf("        return reuse_as_int(if_reuse, value);\n");
    printf("    } else {\n");
    printf("        return reuse_as_int(else_reuse, value);\n");
    printf("    }\n");
    printf("}\n\n");
}

// -- Globals --

Value* NIL;
Value* SYM_T;
Value* SYM_QUOTE;
Value* SYM_IF;
Value* SYM_LAMBDA;
Value* SYM_LET;
Value* SYM_LIFT;
Value* SYM_RUN;
Value* SYM_EM;
Value* SYM_SCAN;
Value* SYM_GET_META;
Value* SYM_SET_META;

// -- Memory Management --

Value* alloc_val(Tag tag) {
    Value* v = malloc(sizeof(Value));
    if (!v) return NULL;
    v->tag = tag;
    return v;
}

Value* mk_int(long i) {
    Value* v = alloc_val(T_INT);
    if (!v) return NULL;
    v->i = i;
    return v;
}

Value* mk_sym(const char* s) {
    if (!s) s = "";
    Value* v = alloc_val(T_SYM);
    if (!v) return NULL;
    v->s = strdup(s);
    if (!v->s) { free(v); return NULL; }
    return v;
}

Value* mk_code(const char* s) {
    if (!s) s = "";
    Value* v = alloc_val(T_CODE);
    if (!v) return NULL;
    v->s = strdup(s);
    if (!v->s) { free(v); return NULL; }
    return v;
}

Value* mk_cons(Value* car, Value* cdr) {
    Value* v = alloc_val(T_CELL);
    if (!v) return NULL;
    v->cell.car = car;
    v->cell.cdr = cdr;
    return v;
}

Value* mk_prim(PrimFn fn) {
    Value* v = alloc_val(T_PRIM);
    if (!v) return NULL;
    v->prim = fn;
    return v;
}

Value* mk_lambda(Value* params, Value* body, Value* env) {
    Value* v = alloc_val(T_LAMBDA);
    if (!v) return NULL;
    v->lam.params = params;
    v->lam.body = body;
    v->lam.env = env;
    return v;
}

// -- Helpers --

int is_nil(Value* v) { return !v || v->tag == T_NIL; }
int is_code(Value* v) { return v && v->tag == T_CODE; }

Value* car(Value* v) { return (v && v->tag == T_CELL) ? v->cell.car : NIL; }
Value* cdr(Value* v) { return (v && v->tag == T_CELL) ? v->cell.cdr : NIL; }

int sym_eq(Value* s1, Value* s2) {
    if (!s1 || !s2) return 0;
    if (s1->tag != T_SYM || s2->tag != T_SYM) return 0;
    if (!s1->s || !s2->s) return 0;
    return strcmp(s1->s, s2->s) == 0;
}

int sym_eq_str(Value* s1, const char* s2) {
    if (!s1 || s1->tag != T_SYM) return 0;
    if (!s1->s || !s2) return 0;
    return strcmp(s1->s, s2) == 0;
}

char* val_to_str(Value* v);

char* list_to_str(Value* v) {
    char buf[1024] = "(";
    while (!is_nil(v)) {
        char* s = val_to_str(car(v));
        if (s) {
            strcat(buf, s);
            free(s);
        }
        v = cdr(v);
        if (!is_nil(v)) strcat(buf, " ");
    }
    strcat(buf, ")");
    return strdup(buf);
}

char* val_to_str(Value* v) {
    if (!v) return strdup("()");  // NULL treated as NIL
    char buf[4096];
    switch (v->tag) {
        case T_INT: sprintf(buf, "%ld", v->i); return strdup(buf);
        case T_SYM: return strdup(v->s);
        case T_CODE: 
            sprintf(buf, "/* CODE */ %s", v->s); 
            return strdup(buf);
        case T_CELL: return list_to_str(v);
        case T_NIL: return strdup("()");
        case T_PRIM: return strdup("#<prim>");
        case T_LAMBDA: return strdup("#<lambda>");
        case T_MENV: return strdup("#<menv>");
        default: return strdup("?");
    }
}

// -- Code Generation Helpers (ASAP) --

Value* emit_c_call(const char* fn, Value* a, Value* b) {
    char* sa = (a->tag == T_CODE) ? a->s : val_to_str(a);
    char* sb = (b->tag == T_CODE) ? b->s : val_to_str(b);

    char ca[512], cb[512];
    if (a->tag == T_INT) snprintf(ca, sizeof(ca), "mk_int(%ld)", a->i);
    else snprintf(ca, sizeof(ca), "%s", sa ? sa : "NULL");

    if (b->tag == T_INT) snprintf(cb, sizeof(cb), "mk_int(%ld)", b->i);
    else snprintf(cb, sizeof(cb), "%s", sb ? sb : "NULL");

    char buf[1280];
    snprintf(buf, sizeof(buf), "%s(%s, %s)", fn, ca, cb);

    if (a->tag != T_CODE) free(sa);
    if (b->tag != T_CODE) free(sb);
    return mk_code(buf);
}

Value* lift_value(Value* v) {
    if (!v) return NIL;
    if (v->tag == T_CODE) return v;
    if (v->tag == T_INT) {
        char buf[64];
        sprintf(buf, "mk_int(%ld)", v->i);
        return mk_code(buf);
    }
    return v; // Default
}

// -- ASAP Strategy Implementation --

void gen_asap_scanner(const char* type_name, int is_list) {
    // ASAP Scanner: Type-specific traversal for static analysis
    // This is NOT a garbage collector - ASAP uses compile-time deallocation.
    // Scanners are useful for:
    //   - Debugging (checking what's reachable)
    //   - Manual reference counting updates
    //   - Verifying memory safety at runtime (debug builds)

    printf("\n// [ASAP] Type-Aware Scanner for %s\n", type_name);
    printf("// Note: ASAP uses compile-time free injection, not runtime GC\n");
    printf("void scan_%s(Obj* x) {\n", type_name);
    printf("  if (!x || x->mark) return;\n");
    printf("  x->mark = 1; // Mark as visited\n");
    if (is_list) {
        printf("  scan_%s(x->a); // Traverse car\n", type_name);
        printf("  scan_%s(x->b); // Traverse cdr\n", type_name);
    }
    printf("}\n\n");

    // Helper to clear marks (for repeated scans)
    printf("void clear_marks_%s(Obj* x) {\n", type_name);
    printf("  if (!x || !x->mark) return;\n");
    printf("  x->mark = 0;\n");
    if (is_list) {
        printf("  clear_marks_%s(x->a);\n", type_name);
        printf("  clear_marks_%s(x->b);\n", type_name);
    }
    printf("}\n");
}

// -- Phase 3 & 8: Typed Reference Fields & Field-Aware Scanner Infrastructure --

typedef enum {
    FIELD_STRONG,    // Normal strong reference
    FIELD_WEAK,      // Weak reference (doesn't prevent deallocation)
    FIELD_UNTRACED   // Non-pointer field (int, float, etc.)
} FieldStrength;

typedef struct TypeField {
    char* name;           // Field name (e.g., "left", "value")
    char* type;           // Field type (e.g., "Tree", "int")
    int is_scannable;     // 1 if this field contains pointers to scan
    FieldStrength strength; // Phase 3: Field reference strength
} TypeField;

typedef struct TypeDef {
    char* name;           // Type name (e.g., "Tree", "List")
    TypeField* fields;    // Array of fields
    int field_count;      // Number of fields
    int is_recursive;     // Type contains pointer to itself
    struct TypeDef* next; // Linked list
} TypeDef;

TypeDef* TYPE_REGISTRY = NULL;

// Phase 3: Ownership graph for back-edge detection
typedef struct OwnershipEdge {
    char* from_type;
    char* field_name;
    char* to_type;
    int is_back_edge;     // Determined by analysis
    struct OwnershipEdge* next;
} OwnershipEdge;

OwnershipEdge* OWNERSHIP_GRAPH = NULL;

void register_type(const char* name, TypeField* fields, int count) {
    TypeDef* t = malloc(sizeof(TypeDef));
    if (!t) return;
    t->name = strdup(name);
    if (!t->name) { free(t); return; }
    t->fields = fields;
    t->field_count = count;
    t->is_recursive = 0;
    t->next = TYPE_REGISTRY;
    TYPE_REGISTRY = t;

    // Check if recursive and initialize field strengths
    for (int i = 0; i < count; i++) {
        if (fields[i].is_scannable) {
            fields[i].strength = FIELD_STRONG;  // Default
            if (strcmp(fields[i].type, name) == 0) {
                t->is_recursive = 1;
            }
        } else {
            fields[i].strength = FIELD_UNTRACED;
        }
    }
}

TypeDef* find_type(const char* name) {
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    return NULL;
}

// Phase 3: Build ownership graph from type definitions
void build_ownership_graph() {
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        for (int i = 0; i < t->field_count; i++) {
            if (t->fields[i].is_scannable) {
                OwnershipEdge* e = malloc(sizeof(OwnershipEdge));
                if (!e) continue;
                e->from_type = strdup(t->name);
                e->field_name = strdup(t->fields[i].name);
                e->to_type = strdup(t->fields[i].type);
                if (!e->from_type || !e->field_name || !e->to_type) {
                    free(e->from_type);
                    free(e->field_name);
                    free(e->to_type);
                    free(e);
                    continue;
                }
                e->is_back_edge = 0;
                e->next = OWNERSHIP_GRAPH;
                OWNERSHIP_GRAPH = e;
            }
        }
        t = t->next;
    }
}

// Phase 3: Detect back-edges using DFS cycle detection
typedef struct VisitState {
    char* type_name;
    int color;  // 0=white, 1=gray, 2=black
    struct VisitState* next;
} VisitState;

VisitState* VISIT_STATES = NULL;

VisitState* find_visit_state(const char* name) {
    VisitState* v = VISIT_STATES;
    while (v) {
        if (strcmp(v->type_name, name) == 0) return v;
        v = v->next;
    }
    return NULL;
}

void add_visit_state(const char* name, int color) {
    VisitState* existing = find_visit_state(name);
    if (existing) {
        existing->color = color;
        return;
    }
    VisitState* v = malloc(sizeof(VisitState));
    if (!v) return;
    v->type_name = strdup(name);
    if (!v->type_name) { free(v); return; }
    v->color = color;
    v->next = VISIT_STATES;
    VISIT_STATES = v;
}

void mark_field_weak(const char* type_name, const char* field_name) {
    TypeDef* t = find_type(type_name);
    if (!t) return;

    for (int i = 0; i < t->field_count; i++) {
        if (strcmp(t->fields[i].name, field_name) == 0) {
            t->fields[i].strength = FIELD_WEAK;
            printf("// AUTO-WEAK: %s.%s\n", type_name, field_name);
            return;
        }
    }
}

// Maximum path depth for back-edge detection (prevents buffer overflow)
#define BACK_EDGE_PATH_MAX 256

void detect_back_edges_dfs(const char* type_name, const char* path[], int path_len) {
    VisitState* v = find_visit_state(type_name);

    // If gray (in current path), we found a cycle
    if (v && v->color == 1) return;
    // If black (fully explored), skip
    if (v && v->color == 2) return;

    // Prevent buffer overflow in path array
    if (path_len >= BACK_EDGE_PATH_MAX) {
        fprintf(stderr, "Warning: type graph depth exceeds %d, skipping back-edge detection\n",
                BACK_EDGE_PATH_MAX);
        return;
    }

    // Mark as gray (being visited)
    add_visit_state(type_name, 1);

    // Add to path
    path[path_len] = type_name;
    path_len++;

    // Visit all outgoing edges
    OwnershipEdge* e = OWNERSHIP_GRAPH;
    while (e) {
        if (strcmp(e->from_type, type_name) == 0) {
            // Check if this edge goes to something in our path (back-edge!)
            for (int i = 0; i < path_len; i++) {
                if (strcmp(path[i], e->to_type) == 0) {
                    // This is a back edge!
                    e->is_back_edge = 1;
                    mark_field_weak(e->from_type, e->field_name);
                }
            }
            // Recurse
            detect_back_edges_dfs(e->to_type, path, path_len);
        }
        e = e->next;
    }

    // Mark as black (fully explored)
    add_visit_state(type_name, 2);
}

void analyze_back_edges() {
    const char* path[BACK_EDGE_PATH_MAX];
    // Reset visit states
    VISIT_STATES = NULL;

    // Start DFS from each type
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        if (find_visit_state(t->name) == NULL) {
            detect_back_edges_dfs(t->name, path, 0);
        }
        t = t->next;
    }
}

void gen_field_aware_scanner(const char* type_name) {
    TypeDef* t = find_type(type_name);
    if (!t) {
        // Fallback to generic list scanner
        gen_asap_scanner(type_name, 1);
        return;
    }

    // Generate field-aware scanner (for traversal/debugging, NOT gc)
    printf("\n// [ASAP] Field-Aware Scanner for %s\n", type_name);
    printf("// Traverses only pointer fields, skips value fields\n");
    printf("void scan_%s(%s* x) {\n", type_name, type_name);
    printf("  if (!x || x->mark) return;\n");
    printf("  x->mark = 1;\n");

    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable) {
            printf("  scan_%s(x->%s); // Traverse %s\n",
                   t->fields[i].type, t->fields[i].name, t->fields[i].name);
        } else {
            printf("  // Skip value field: %s\n", t->fields[i].name);
        }
    }

    printf("}\n");
}

// Phase 3: Generate weak reference handling code
void gen_weak_ref_runtime() {
    printf("// Phase 3: Weak Reference Support (Auto-detected back-edges)\n");
    printf("typedef struct WeakRef {\n");
    printf("    void* target;\n");
    printf("    int alive;  // 1 if target is still valid\n");
    printf("} WeakRef;\n\n");

    printf("WeakRef* mk_weak_ref(void* target) {\n");
    printf("    WeakRef* w = malloc(sizeof(WeakRef));\n");
    printf("    if (!w) return NULL;\n");
    printf("    w->target = target;\n");
    printf("    w->alive = 1;\n");
    printf("    return w;\n");
    printf("}\n\n");

    printf("void* deref_weak(WeakRef* w) {\n");
    printf("    if (w && w->alive) return w->target;\n");
    printf("    return NULL;\n");
    printf("}\n\n");

    printf("void invalidate_weak(WeakRef* w) {\n");
    printf("    if (w) w->alive = 0;\n");
    printf("}\n\n");
}

// Generate struct definition with weak refs
void gen_struct_def(TypeDef* t) {
    printf("typedef struct %s {\n", t->name);
    printf("    int _rc;           // Reference count\n");
    printf("    int _weak_rc;      // Weak reference count\n");

    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable) {
            if (t->fields[i].strength == FIELD_WEAK) {
                printf("    WeakRef* %s;  // WEAK (auto-detected back-edge)\n",
                       t->fields[i].name);
            } else {
                printf("    struct %s* %s;  // STRONG\n",
                       t->fields[i].type, t->fields[i].name);
            }
        } else {
            printf("    int %s;  // VALUE\n", t->fields[i].name);
        }
    }

    printf("} %s;\n\n", t->name);
}

// Generate release function that handles weak refs
void gen_release_func(TypeDef* t) {
    printf("void release_%s(%s* obj) {\n", t->name, t->name);
    printf("    if (!obj) return;\n");
    printf("    obj->_rc--;\n");
    printf("    if (obj->_rc == 0) {\n");

    // Release strong children only
    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable && t->fields[i].strength == FIELD_STRONG) {
            printf("        release_%s(obj->%s);\n", t->fields[i].type, t->fields[i].name);
        }
    }

    printf("        // Check weak refs before freeing\n");
    printf("        if (obj->_weak_rc == 0) {\n");
    printf("            free(obj);\n");
    printf("        } else {\n");
    printf("            obj->_rc = -1;  // Mark as dead\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");
}

// Initialize built-in types
void init_type_registry() {
    // Register Pair type (used by cons)
    static TypeField pair_fields[] = {
        {"a", "Obj", 1, FIELD_STRONG},  // car - scannable
        {"b", "Obj", 1, FIELD_STRONG}   // cdr - scannable
    };
    register_type("Pair", pair_fields, 2);

    // Register List type (alias for Pair in this implementation)
    static TypeField list_fields[] = {
        {"a", "List", 1, FIELD_STRONG},  // car - scannable
        {"b", "List", 1, FIELD_STRONG}   // cdr - scannable
    };
    register_type("List", list_fields, 2);

    // Example: Tree type with mixed fields
    static TypeField tree_fields[] = {
        {"left", "Tree", 1, FIELD_STRONG},   // Left child - scannable
        {"right", "Tree", 1, FIELD_STRONG},  // Right child - scannable
        {"value", "int", 0, FIELD_UNTRACED}  // Integer value - NOT scannable
    };
    register_type("Tree", tree_fields, 3);

    // Phase 3 Example: Doubly-linked list (has back-edges)
    // The 'prev' field should be auto-detected as a back-edge
    static TypeField dll_fields[] = {
        {"value", "int", 0, FIELD_UNTRACED},  // Value field
        {"next", "DLLNode", 1, FIELD_STRONG}, // Forward link
        {"prev", "DLLNode", 1, FIELD_STRONG}  // Back link (will be marked WEAK)
    };
    register_type("DLLNode", dll_fields, 3);

    // Phase 3 Example: Tree with parent pointers
    static TypeField tree_parent_fields[] = {
        {"left", "TreeWithParent", 1, FIELD_STRONG},
        {"right", "TreeWithParent", 1, FIELD_STRONG},
        {"parent", "TreeWithParent", 1, FIELD_STRONG}, // Will be marked WEAK
        {"value", "int", 0, FIELD_UNTRACED}
    };
    register_type("TreeWithParent", tree_parent_fields, 4);

    // Build ownership graph and detect back-edges
    build_ownership_graph();
    analyze_back_edges();
}

// -- Environment --

Value* env_lookup(Value* env, Value* sym) {
    while (!is_nil(env)) {
        Value* pair = car(env);
        if (sym_eq(car(pair), sym)) return cdr(pair);
        env = cdr(env);
    }
    return NULL;
}

Value* env_extend(Value* env, Value* sym, Value* val) {
    return mk_cons(mk_cons(sym, val), env);
}

// -- ASAP Analysis Functions --

AnalysisContext* mk_analysis_ctx() {
    AnalysisContext* ctx = malloc(sizeof(AnalysisContext));
    if (!ctx) return NULL;
    ctx->vars = NULL;
    ctx->current_depth = 0;
    ctx->in_lambda = 0;
    return ctx;
}

void free_analysis_ctx(AnalysisContext* ctx) {
    if (!ctx) return;
    VarUsage* v = ctx->vars;
    while (v) {
        VarUsage* next = v->next;
        free(v->name);
        free(v);
        v = next;
    }
    free(ctx);
}

VarUsage* find_var(AnalysisContext* ctx, const char* name) {
    if (!ctx) return NULL;
    VarUsage* v = ctx->vars;
    while (v) {
        if (strcmp(v->name, name) == 0) return v;
        v = v->next;
    }
    return NULL;
}

VarUsage* add_var(AnalysisContext* ctx, const char* name) {
    if (!ctx) return NULL;
    VarUsage* v = malloc(sizeof(VarUsage));
    if (!v) return NULL;
    v->name = strdup(name);
    if (!v->name) {
        free(v);
        return NULL;
    }
    v->use_count = 0;
    v->last_use_depth = -1;
    v->escape_class = ESCAPE_NONE;
    v->captured_by_lambda = 0;
    v->freed = 0;
    v->next = ctx->vars;
    ctx->vars = v;
    return v;
}

void record_var_use(AnalysisContext* ctx, const char* name) {
    if (!ctx) return;
    VarUsage* v = find_var(ctx, name);
    if (v) {
        v->use_count++;
        v->last_use_depth = ctx->current_depth;
        if (ctx->in_lambda) {
            v->captured_by_lambda = 1;
        }
    }
}

// Forward declaration for analyze_expr
void analyze_expr(Value* expr, AnalysisContext* ctx);

void analyze_list(Value* list, AnalysisContext* ctx) {
    while (!is_nil(list)) {
        analyze_expr(car(list), ctx);
        list = cdr(list);
    }
}

void analyze_expr(Value* expr, AnalysisContext* ctx) {
    if (!expr || !ctx || is_nil(expr)) return;

    ctx->current_depth++;

    switch (expr->tag) {
        case T_SYM:
            record_var_use(ctx, expr->s);
            break;

        case T_CELL: {
            Value* op = car(expr);
            Value* args = cdr(expr);

            // Handle special forms
            if (op->tag == T_SYM) {
                if (strcmp(op->s, "quote") == 0) {
                    // Don't analyze quoted expressions
                } else if (strcmp(op->s, "lambda") == 0) {
                    // Lambda captures variables - mark in_lambda
                    int saved_in_lambda = ctx->in_lambda;
                    ctx->in_lambda = 1;
                    // Analyze body (skip params)
                    if (!is_nil(args) && !is_nil(cdr(args))) {
                        analyze_expr(car(cdr(args)), ctx);
                    }
                    ctx->in_lambda = saved_in_lambda;
                } else if (strcmp(op->s, "let") == 0) {
                    // Analyze binding value and body
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));
                    // Analyze each binding's value expression
                    while (!is_nil(bindings)) {
                        Value* bind = car(bindings);
                        if (!is_nil(bind) && !is_nil(cdr(bind))) {
                            analyze_expr(car(cdr(bind)), ctx);
                        }
                        bindings = cdr(bindings);
                    }
                    analyze_expr(body, ctx);
                } else if (strcmp(op->s, "if") == 0) {
                    analyze_list(args, ctx);
                } else {
                    // Regular application
                    analyze_expr(op, ctx);
                    analyze_list(args, ctx);
                }
            } else {
                // Application with non-symbol operator
                analyze_expr(op, ctx);
                analyze_list(args, ctx);
            }
            break;
        }
        default:
            break;
    }

    ctx->current_depth--;
}

// Analyze escape: determine if a variable escapes its scope
void analyze_escape(Value* expr, AnalysisContext* ctx, EscapeClass context) {
    if (!expr || !ctx || is_nil(expr)) return;

    switch (expr->tag) {
        case T_SYM: {
            VarUsage* v = find_var(ctx, expr->s);
            if (v && context > v->escape_class) {
                v->escape_class = context;
            }
            break;
        }
        case T_CELL: {
            Value* op = car(expr);
            Value* args = cdr(expr);

            if (op->tag == T_SYM) {
                if (strcmp(op->s, "lambda") == 0) {
                    // Variables in lambda body escape globally (captured)
                    int saved = ctx->in_lambda;
                    ctx->in_lambda = 1;
                    if (!is_nil(args) && !is_nil(cdr(args))) {
                        analyze_escape(car(cdr(args)), ctx, ESCAPE_GLOBAL);
                    }
                    ctx->in_lambda = saved;
                } else if (strcmp(op->s, "let") == 0) {
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));
                    while (!is_nil(bindings)) {
                        Value* bind = car(bindings);
                        if (!is_nil(bind) && !is_nil(cdr(bind))) {
                            analyze_escape(car(cdr(bind)), ctx, ESCAPE_NONE);
                        }
                        bindings = cdr(bindings);
                    }
                    analyze_escape(body, ctx, context);
                } else if (strcmp(op->s, "cons") == 0) {
                    // Arguments to cons escape (stored in heap structure)
                    while (!is_nil(args)) {
                        analyze_escape(car(args), ctx, ESCAPE_ARG);
                        args = cdr(args);
                    }
                } else {
                    // Regular function call - args escape to callee
                    while (!is_nil(args)) {
                        analyze_escape(car(args), ctx, ESCAPE_ARG);
                        args = cdr(args);
                    }
                }
            }
            break;
        }
        default:
            break;
    }
}

// Find free variables in an expression (for lambda capture analysis)
Value* find_free_vars(Value* expr, Value* bound) {
    if (!expr || is_nil(expr)) return NIL;

    if (expr->tag == T_SYM) {
        // Check if bound
        if (!env_lookup(bound, expr)) {
            return mk_cons(expr, NIL);
        }
        return NIL;
    }

    if (expr->tag == T_CELL) {
        Value* op = car(expr);
        Value* args = cdr(expr);

        if (op->tag == T_SYM) {
            if (strcmp(op->s, "quote") == 0) {
                return NIL;
            }
            if (strcmp(op->s, "lambda") == 0) {
                // Extend bound with lambda params
                Value* params = car(args);
                Value* body = car(cdr(args));
                Value* new_bound = bound;
                while (!is_nil(params)) {
                    new_bound = env_extend(new_bound, car(params), SYM_T);
                    params = cdr(params);
                }
                return find_free_vars(body, new_bound);
            }
            if (strcmp(op->s, "let") == 0) {
                // Process let bindings
                Value* bindings = car(args);
                Value* body = car(cdr(args));
                Value* result = NIL;
                Value* new_bound = bound;

                while (!is_nil(bindings)) {
                    Value* bind = car(bindings);
                    Value* sym = car(bind);
                    Value* val_expr = car(cdr(bind));

                    // Free vars in value expression
                    Value* val_fvs = find_free_vars(val_expr, bound);
                    // Append to result
                    while (!is_nil(val_fvs)) {
                        result = mk_cons(car(val_fvs), result);
                        val_fvs = cdr(val_fvs);
                    }
                    new_bound = env_extend(new_bound, sym, SYM_T);
                    bindings = cdr(bindings);
                }

                Value* body_fvs = find_free_vars(body, new_bound);
                while (!is_nil(body_fvs)) {
                    result = mk_cons(car(body_fvs), result);
                    body_fvs = cdr(body_fvs);
                }
                return result;
            }
        }

        // Recurse on all subexpressions
        Value* result = NIL;
        result = find_free_vars(op, bound);
        while (!is_nil(args)) {
            Value* sub_fvs = find_free_vars(car(args), bound);
            while (!is_nil(sub_fvs)) {
                result = mk_cons(car(sub_fvs), result);
                sub_fvs = cdr(sub_fvs);
            }
            args = cdr(args);
        }
        return result;
    }

    return NIL;
}

// -- Handlers & MEnv --

// Forward decl
Value* eval(Value* expr, Value* menv);
Value* mk_menv(Value* parent, Value* env);

// Default Handlers (The "Base" Evaluator)

Value* h_lit_default(Value* exp, Value* menv) {
    return exp; // Integers evaluate to themselves
}

Value* h_var_default(Value* exp, Value* menv) {
    Value* v = env_lookup(menv->menv.env, exp);
    if (!v) { printf("Error: Unbound %s\n", exp->s); return NIL; }
    return v;
}

Value* eval_list(Value* list, Value* menv) {
    if (is_nil(list)) return NIL;
    Value* h = eval(car(list), menv);
    Value* t = eval_list(cdr(list), menv);
    return mk_cons(h, t);
}

Value* h_app_default(Value* exp, Value* menv) {
    // exp is (f arg1 arg2 ...)
    Value* f_expr = car(exp);
    Value* args_expr = cdr(exp);
    
    // Check for special forms that look like apps but are handled by eval dispatcher
    // (Actually eval dispatcher calls h_app only for "unknown" heads that turn out to be functions)
    // But here we need to EVAL the function position first.
    
    Value* fn = eval(f_expr, menv);
    Value* args = eval_list(args_expr, menv);
    
    // Stage Polymorphism for Primitives
    if (fn->tag == T_PRIM) return fn->prim(args, menv);
    
    if (fn->tag == T_LAMBDA) {
        Value* params = fn->lam.params;
        Value* body = fn->lam.body;
        Value* closure_env = fn->lam.env; // Lexical scope
        
        // We create a NEW MEnv for the function body execution
        // It inherits Handlers from 'menv' (dynamic scope for reflection) 
        // but uses 'closure_env' extended with args for bindings (lexical scope for vars)
        
        Value* new_env = closure_env;
        Value* p = params; 
        Value* a = args;
        while (!is_nil(p) && !is_nil(a)) {
            new_env = env_extend(new_env, car(p), car(a));
            p = cdr(p); a = cdr(a);
        }
        
        // Construct MEnv for body: same parent/handlers as current, new env
        Value* body_menv = mk_menv(menv->menv.parent, new_env);
        // Copy handlers (shallow copy of pointers)
        body_menv->menv.h_app = menv->menv.h_app;
        body_menv->menv.h_let = menv->menv.h_let;
        body_menv->menv.h_if  = menv->menv.h_if;
        
        return eval(body, body_menv);
    }
    
    char* fn_str = val_to_str(fn);
    printf("Error: Not a function: %s\n", fn_str ? fn_str : "(null)");
    free(fn_str);
    return NIL;
}

Value* h_let_default(Value* exp, Value* menv) {
    // Phase 7: Multi-binding let support
    // (let ((x v1) (y v2) ...) body) or (let ((x v)) body)
    Value* args = cdr(exp);
    Value* bindings = car(args);
    Value* body = car(cdr(args));

    // Check if any binding produces code (compilation mode)
    int any_code = 0;
    Value* check_bindings = bindings;
    Value* new_env = menv->menv.env;

    // First pass: evaluate all bindings and check for code
    typedef struct BindingInfo {
        Value* sym;
        Value* val;
        struct BindingInfo* next;
    } BindingInfo;

    BindingInfo* bind_list = NULL;
    BindingInfo* bind_tail = NULL;

    while (!is_nil(check_bindings)) {
        Value* bind = car(check_bindings);
        Value* sym = car(bind);
        Value* val_expr = car(cdr(bind));
        Value* val = eval(val_expr, menv);

        if (val->tag == T_CODE) any_code = 1;

        BindingInfo* info = malloc(sizeof(BindingInfo));
        if (!info) {
            fprintf(stderr, "Error: OOM in let binding\n");
            // Free already-allocated bindings
            while (bind_list) {
                BindingInfo* next = bind_list->next;
                free(bind_list);
                bind_list = next;
            }
            return NIL;
        }
        info->sym = sym;
        info->val = val;
        info->next = NULL;

        if (bind_tail) {
            bind_tail->next = info;
            bind_tail = info;
        } else {
            bind_list = bind_tail = info;
        }

        check_bindings = cdr(check_bindings);
    }

    if (any_code) {
        // Compilation mode: generate C code with analysis
        AnalysisContext* ctx = mk_analysis_ctx();
        ShapeContext* shape_ctx = mk_shape_context();  // Phase 2: Shape analysis

        // Register all bound variables for analysis
        BindingInfo* b = bind_list;
        while (b) {
            add_var(ctx, b->sym->s);
            b = b->next;
        }

        // Analyze body for variable usage
        analyze_expr(body, ctx);
        analyze_escape(body, ctx, ESCAPE_NONE);

        // Phase 2: Analyze shapes for optimal deallocation strategy
        analyze_shapes_expr(exp, shape_ctx);

        // Build declarations and extend environment using dynamic strings
        DString* all_decls = ds_new();
        DString* all_frees = ds_new();
        if (!all_decls || !all_frees) {
            ds_free(all_decls);
            ds_free(all_frees);
            free_analysis_ctx(ctx);
            free_shape_context(shape_ctx);
            while (bind_list) {
                BindingInfo* next = bind_list->next;
                free(bind_list);
                bind_list = next;
            }
            fprintf(stderr, "Error: OOM in let codegen\n");
            return NIL;
        }

        b = bind_list;
        while (b) {
            VarUsage* usage = find_var(ctx, b->sym->s);
            int is_captured = usage ? usage->captured_by_lambda : 0;
            int use_count = usage ? usage->use_count : 0;
            int escape_class = usage ? usage->escape_class : ESCAPE_NONE;

            // Phase 2: Get shape for this variable
            ShapeInfo* shape_info = find_shape(shape_ctx, b->sym->s);
            Shape var_shape = shape_info ? shape_info->shape : SHAPE_UNKNOWN;

            char* val_str = (b->val->tag == T_CODE) ? b->val->s : val_to_str(b->val);
            const char* safe_val_str = val_str ? val_str : "NULL";

            // For non-code values in a code context, lift them
            if (b->val->tag != T_CODE) {
                if (b->val->tag == T_INT) {
                    ds_printf(all_decls, "  Obj* %s = mk_int(%ld);\n", b->sym->s, b->val->i);
                } else {
                    ds_printf(all_decls, "  Obj* %s = %s;\n", b->sym->s, safe_val_str);
                }
            } else if (escape_class == ESCAPE_NONE && !is_captured) {
                ds_printf(all_decls, "  Obj* %s = %s; // stack-candidate\n", b->sym->s, safe_val_str);
            } else {
                ds_printf(all_decls, "  Obj* %s = %s;\n", b->sym->s, safe_val_str);
            }

            // Phase 2: Use shape-based free strategy
            const char* free_fn = shape_free_strategy(var_shape);

            // Build free statements (in reverse order for proper stack-like cleanup)
            if (!is_captured && use_count > 0) {
                const char* shape_name = var_shape == SHAPE_TREE ? "TREE" :
                                         var_shape == SHAPE_DAG ? "DAG" :
                                         var_shape == SHAPE_CYCLIC ? "CYCLIC" : "UNKNOWN";
                // Prepend to all_frees for reverse order
                DString* new_frees = ds_new();
                if (new_frees) {
                    ds_printf(new_frees, "  %s(%s); // ASAP Clean (shape: %s)\n",
                              free_fn, b->sym->s, shape_name);
                    ds_append(new_frees, ds_cstr(all_frees));
                    ds_free(all_frees);
                    all_frees = new_frees;
                }
            } else if (!is_captured && use_count == 0) {
                // Unused - will free right after declaration in the block
                ds_printf(all_decls, "  %s(%s); // ASAP: unused\n", free_fn, b->sym->s);
            } else if (is_captured) {
                ds_printf(all_frees, "  // %s captured by closure - no free\n", b->sym->s);
            }

            // Extend environment with code reference
            Value* ref = mk_code(b->sym->s);
            new_env = env_extend(new_env, b->sym, ref);

            if (b->val->tag != T_CODE) free(val_str);
            b = b->next;
        }

        // Create body menv
        Value* body_menv = mk_menv(menv->menv.parent, new_env);
        body_menv->menv.h_app = menv->menv.h_app;
        body_menv->menv.h_let = menv->menv.h_let;

        Value* res = eval(body, body_menv);
        int sres_owned = (!res || res->tag != T_CODE);
        char* sres = (res && res->tag == T_CODE) ? res->s : val_to_str(res);

        // Build final block using dynamic string
        DString* block = ds_new();
        if (!block) {
            ds_free(all_decls);
            ds_free(all_frees);
            if (sres_owned) free(sres);
            free_analysis_ctx(ctx);
            free_shape_context(shape_ctx);
            while (bind_list) {
                BindingInfo* next = bind_list->next;
                free(bind_list);
                bind_list = next;
            }
            fprintf(stderr, "Error: OOM in let codegen\n");
            return NIL;
        }
        ds_printf(block, "{\n%s  Obj* _res = %s;\n%s  _res;\n}",
                  ds_cstr(all_decls), sres ? sres : "NULL", ds_cstr(all_frees));

        char* block_str = ds_take(block);
        ds_free(all_decls);
        ds_free(all_frees);

        if (sres_owned) free(sres);
        free_analysis_ctx(ctx);
        free_shape_context(shape_ctx);

        // Free binding info list
        while (bind_list) {
            BindingInfo* next = bind_list->next;
            free(bind_list);
            bind_list = next;
        }

        Value* result = mk_code(block_str);
        free(block_str);
        return result;
    }

    // Interpretation mode: extend environment with all bindings
    BindingInfo* b = bind_list;
    while (b) {
        new_env = env_extend(new_env, b->sym, b->val);
        b = b->next;
    }

    Value* body_menv = mk_menv(menv->menv.parent, new_env);
    body_menv->menv.h_app = menv->menv.h_app;
    body_menv->menv.h_let = menv->menv.h_let;

    // Free binding info list
    while (bind_list) {
        BindingInfo* next = bind_list->next;
        free(bind_list);
        bind_list = next;
    }

    return eval(body, body_menv);
}

Value* h_if_default(Value* exp, Value* menv) {
    Value* args = cdr(exp);
    Value* cond_expr = car(args);
    Value* then_expr = car(cdr(args));
    Value* else_expr = car(cdr(cdr(args)));
    
    Value* c = eval(cond_expr, menv);
    
    if (is_code(c)) {
        Value* t = eval(then_expr, menv);
        Value* e = eval(else_expr, menv);
        char buf[2048];
        char* sc = c->s;
        int st_owned = (!t || t->tag != T_CODE);
        int se_owned = (!e || e->tag != T_CODE);
        char* st = (t && t->tag == T_CODE) ? t->s : val_to_str(t);
        char* se = (e && e->tag == T_CODE) ? e->s : val_to_str(e);
        sprintf(buf, "(if %s %s %s)", sc, st ? st : "NULL", se ? se : "NULL");
        if (st_owned) free(st);
        if (se_owned) free(se);
        return mk_code(buf);
    }
    
    if (!is_nil(c)) return eval(then_expr, menv);
    else return eval(else_expr, menv);
}

Value* mk_menv(Value* parent, Value* env) {
    Value* v = alloc_val(T_MENV);
    if (!v) return NULL;
    v->menv.env = env;
    v->menv.parent = parent;
    // Default handlers
    v->menv.h_app = h_app_default;
    v->menv.h_let = h_let_default;
    v->menv.h_if  = h_if_default;
    v->menv.h_lit = h_lit_default;
    v->menv.h_var = h_var_default;
    return v;
}

// -- Evaluator (Dispatcher) --

Value* eval(Value* expr, Value* menv) {
    if (is_nil(expr)) return NIL;
    if (expr->tag == T_INT) return menv->menv.h_lit(expr, menv);
    if (expr->tag == T_CODE) return expr; // Self-evaluating
    
    if (expr->tag == T_SYM) {
        return menv->menv.h_var(expr, menv);
    }
    
    if (expr->tag == T_CELL) {
        Value* op = car(expr);
        Value* args = cdr(expr);
        
        // Special Forms (Hardcoded for now, could be handlers too)
        if (sym_eq(op, SYM_QUOTE)) return car(args);
        if (sym_eq(op, SYM_LIFT)) return lift_value(eval(car(args), menv));
        
        if (sym_eq(op, SYM_IF)) return menv->menv.h_if(expr, menv);
        if (sym_eq(op, SYM_LET)) return menv->menv.h_let(expr, menv);
        
        if (sym_eq(op, SYM_LAMBDA)) {
            Value* params = car(args);
            Value* body = car(cdr(args));
            return mk_lambda(params, body, menv->menv.env);
        }
        
        // EM (Execute Meta): Jump to parent level
        if (sym_eq(op, SYM_EM)) {
            Value* e = car(args);
            Value* parent = menv->menv.parent;
            if (is_nil(parent)) {
                // Lazy tower: create new meta-level on demand
                parent = mk_menv(NIL, NIL); // Empty env for meta-level
                menv->menv.parent = parent;
            }
            return eval(e, parent);
        }
        
        // set-meta! 'key val
        if (sym_eq(op, SYM_SET_META)) {
            Value* key = eval(car(args), menv); // Evaluate key? usually key is symbol
            if (key->tag != T_SYM) key = car(args); // Quote it implicitly if simple symbol?
            
            Value* val = eval(car(cdr(args)), menv); // New handler function
            
            if (sym_eq_str(key, "add")) {
                // Mocking: We don't have a 'h_add' slot, we modify the ENV binding for +
                // Real Purple modifies handler tables.
                // For 'app', 'let', 'if', we have slots.
                // Let's support modifying 'if' behavior?
                // Or just bindings in the meta-env?
                // "Reflective languages allow modifying the evaluator"
                // Let's modify 'h_app' to intercept application?
                // Too complex for C function pointers unless we wrap 'val' in a C-callable thunk.
                // Simplified: We assume 'val' is a Lambda and we store it in a global or special slot.
                // Let's just update the current environment for 'add' as a proof of concept for "Reflection"
                // (Modifying the environment *is* a form of reflection if done via meta-level APIs)
                menv->menv.env = env_extend(menv->menv.env, mk_sym("+"), val);
            }
            return NIL;
        }
        
        if (sym_eq(op, SYM_SCAN)) {
            Value* type_sym = eval(car(args), menv);
            Value* val = eval(car(cdr(args)), menv);
            char buf[256];
            int sval_owned = (!val || val->tag != T_CODE);
            char* sval = (val && val->tag == T_CODE) ? val->s : val_to_str(val);
            sprintf(buf, "scan_%s(%s); // ASAP Mark", type_sym->s, sval ? sval : "NULL");
            if (sval_owned) free(sval);
            return mk_code(buf);
        }

        // Application Handler
        return menv->menv.h_app(expr, menv);
    }
    return NIL;
}

// -- Primitives --

Value* prim_add(Value* args, Value* menv) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (is_code(a) || is_code(b)) return emit_c_call("add", a, b);
    return mk_int(a->i + b->i);
}

Value* prim_sub(Value* args, Value* menv) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (is_code(a) || is_code(b)) return emit_c_call("sub", a, b);
    return mk_int(a->i - b->i);
}

Value* prim_cons(Value* args, Value* menv) {
    Value* a = car(args);
    Value* b = car(cdr(args));
    if (is_code(a) || is_code(b)) return emit_c_call("mk_pair", a, b);
    return mk_cons(a, b);
}

Value* prim_run(Value* args, Value* menv) {
    return eval(car(args), menv);
}

// -- Reader (Same as before) --
const char* parse_ptr;
Value* parse();
void skip_ws() { while (isspace(*parse_ptr)) parse_ptr++; }
Value* parse_list() {
    skip_ws();
    if (*parse_ptr == ')') { parse_ptr++; return NIL; }
    Value* head = parse();
    Value* tail = parse_list();
    return mk_cons(head, tail);
}
Value* parse() {
    skip_ws();
    if (*parse_ptr == '\0') return NULL;
    if (*parse_ptr == '(') { parse_ptr++; return parse_list(); }
    if (*parse_ptr == '\'') { parse_ptr++; Value* v = parse(); return mk_cons(SYM_QUOTE, mk_cons(v, NIL)); }
    if (isdigit(*parse_ptr) || (*parse_ptr == '-' && isdigit(parse_ptr[1]))) {
        errno = 0;
        char* endptr;
        long i = strtol(parse_ptr, &endptr, 10);
        if (errno == ERANGE) {
            fprintf(stderr, "Parse error: integer overflow\n");
            parse_ptr = endptr;
            return NIL;
        }
        parse_ptr = endptr;
        return mk_int(i);
    }
    const char* start = parse_ptr;
    while (*parse_ptr && !isspace(*parse_ptr) && *parse_ptr != ')' && *parse_ptr != '(') parse_ptr++;
    char* s = strndup(start, parse_ptr - start);
    Value* sym = mk_sym(s);
    free(s);
    return sym;
}

// -- Main --

void init_syms() {
    NIL = alloc_val(T_NIL);
    SYM_T = mk_sym("t");
    SYM_QUOTE = mk_sym("quote");
    SYM_IF = mk_sym("if");
    SYM_LAMBDA = mk_sym("lambda");
    SYM_LET = mk_sym("let");
    SYM_LIFT = mk_sym("lift");
    SYM_RUN = mk_sym("run");
    SYM_EM = mk_sym("EM");
    SYM_SCAN = mk_sym("scan");
    SYM_GET_META = mk_sym("get-meta");
    SYM_SET_META = mk_sym("set-meta!");
}

int main(int argc, char** argv) {
    init_syms();
    init_type_registry();  // Phase 8: Initialize type registry for field-aware scanners

    // Initial Environment
    Value* env = NIL;
    env = env_extend(env, mk_sym("+"), mk_prim(prim_add));
    env = env_extend(env, mk_sym("-"), mk_prim(prim_sub));
    env = env_extend(env, mk_sym("cons"), mk_prim(prim_cons));
    env = env_extend(env, mk_sym("run"), mk_prim(prim_run));
    
    // Initial Meta-Environment (Level 0)
    Value* menv = mk_menv(NIL, env);
    
    printf("// Purple + ASAP C Compiler Output\n");
    printf("// Optimizations based on 'Practical Static Memory Management' (Corbyn 2020):\n");
    printf("// 1. Inlined Mark Bits: 'mark' field in Obj avoids external sets.\n");
    printf("// 2. Deferred Freeing: 'free_obj' queues pointers to prevent double-frees/dangling usage during scan.\n");
    printf("// 3. (Future) Compact Paths: Could optimize scan_List for complex graphs.\n\n");

    printf("// Runtime Header\n");
    printf("#include <stdlib.h>\n");
    printf("#include <stdint.h>\n");
    printf("typedef struct Obj { int mark; union { long i; struct { struct Obj *a, *b; }; }; } Obj;\n\n");

    // Phase 5: Dynamic Free List (linked list instead of fixed array)
    printf("// Phase 5: Dynamic Free List\n");
    printf("typedef struct FreeNode { Obj* obj; struct FreeNode* next; } FreeNode;\n");
    printf("FreeNode* FREE_HEAD = NULL;\n");
    printf("int FREE_COUNT = 0;\n\n");

    // Phase 3: Stack allocation pool for non-escaping values
    printf("// Phase 3: Stack Allocation Pool\n");
    printf("#define STACK_POOL_SIZE 256\n");
    printf("Obj STACK_POOL[STACK_POOL_SIZE];\n");
    printf("int STACK_PTR = 0;\n\n");

    // Helper function to check if pointer is in stack pool (using uintptr_t to avoid UB)
    printf("// Helper to check stack allocation (uses uintptr_t to avoid UB)\n");
    printf("static int is_stack_obj(Obj* x) {\n");
    printf("    uintptr_t px = (uintptr_t)x;\n");
    printf("    uintptr_t start = (uintptr_t)&STACK_POOL[0];\n");
    printf("    uintptr_t end = (uintptr_t)&STACK_POOL[STACK_POOL_SIZE];\n");
    printf("    return px >= start && px < end;\n");
    printf("}\n\n");

    printf("Obj* mk_int(long i) { Obj* x = malloc(sizeof(Obj)); if (!x) return NULL; x->mark=0; x->i=i; return x; }\n");
    printf("Obj* mk_pair(Obj* a, Obj* b) { Obj* x = malloc(sizeof(Obj)); if (!x) return NULL; x->mark=0; x->a=a; x->b=b; return x; }\n\n");

    // Phase 2: Shape-based deallocation functions
    printf("// Phase 2: Shape-based deallocation (Ghiya-Hendren analysis)\n");
    printf("// TREE: No sharing, direct free without refcount\n");
    printf("void free_tree(Obj* x) {\n");
    printf("  if (!x) return;\n");
    printf("  if (is_stack_obj(x)) return;\n");
    printf("  // For tree-shaped data, recursively free children\n");
    printf("  if (x->a) free_tree(x->a);\n");
    printf("  if (x->b) free_tree(x->b);\n");
    printf("  free(x);\n");
    printf("}\n\n");

    printf("// DAG: Sharing without cycles, use reference counting\n");
    printf("void dec_ref(Obj* x) {\n");
    printf("  if (!x) return;\n");
    printf("  if (is_stack_obj(x)) return;\n");
    printf("  // Simple refcount decrement (assumes mark field used as refcount)\n");
    printf("  x->mark--;\n");
    printf("  if (x->mark <= 0) {\n");
    printf("    dec_ref(x->a);\n");
    printf("    dec_ref(x->b);\n");
    printf("    free(x);\n");
    printf("  }\n");
    printf("}\n\n");

    printf("// CYCLIC: May have cycles, needs deferred collection\n");
    printf("void dec_ref_cyclic(Obj* x) {\n");
    printf("  if (!x) return;\n");
    printf("  if (is_stack_obj(x)) return;\n");
    printf("  // Add to free list for batch processing (arena-style)\n");
    printf("  free_obj(x);\n");
    printf("}\n\n");

    // Phase 3: Generate weak reference support
    gen_weak_ref_runtime();

    // Phase 4: Generate Perceus reuse runtime
    gen_perceus_runtime();

    // Stack-allocated versions
    printf("// Stack-allocated versions for non-escaping values\n");
    printf("Obj* mk_int_stack(long i) {\n");
    printf("  if (STACK_PTR < STACK_POOL_SIZE) {\n");
    printf("    Obj* x = &STACK_POOL[STACK_PTR++];\n");
    printf("    x->mark = 0; x->i = i;\n");
    printf("    return x;\n");
    printf("  }\n");
    printf("  return mk_int(i); // Fallback to heap\n");
    printf("}\n\n");

    // Dynamic free_obj with linked list
    printf("void free_obj(Obj* x) {\n");
    printf("  if (!x) return;\n");
    printf("  // Check if stack-allocated (don't free stack objects)\n");
    printf("  if (is_stack_obj(x)) return;\n");
    printf("  FreeNode* n = malloc(sizeof(FreeNode));\n");
    printf("  if (!n) { free(x); return; } // OOM fallback: free immediately\n");
    printf("  n->obj = x;\n");
    printf("  n->next = FREE_HEAD;\n");
    printf("  FREE_HEAD = n;\n");
    printf("  FREE_COUNT++;\n");
    printf("}\n\n");

    printf("void flush_freelist() {\n");
    printf("  while (FREE_HEAD) {\n");
    printf("    FreeNode* n = FREE_HEAD;\n");
    printf("    FREE_HEAD = n->next;\n");
    printf("    free(n->obj);\n");
    printf("    free(n);\n");
    printf("  }\n");
    printf("  FREE_COUNT = 0;\n");
    printf("  STACK_PTR = 0; // Reset stack pool\n");
    printf("}\n\n");
    
    gen_asap_scanner("List", 1);
    
    printf("\n// Main Program\n");
    printf("int main() {\n");
    
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), stdin)) {
        parse_ptr = buffer;
        Value* expr = parse();
        if (!expr) break;
        Value* result = eval(expr, menv);
        if (!result) {
            printf("  // Error: eval returned NULL\n");
            continue;
        }
        if (result->tag == T_CODE)
            printf("  %s;\n", result->s);
        else {
            char* result_str = val_to_str(result);
            printf("  // Result: %s\n", result_str ? result_str : "(null)");
            free(result_str);
        }
        
        printf("  flush_freelist();\n");
    }
    printf("  return 0;\n}\n");
    return 0;
}
