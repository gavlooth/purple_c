#include "escape.h"
#include <string.h>

// Global analysis context
AnalysisContext* g_analysis_ctx = NULL;

// -- Context Management --

AnalysisContext* mk_analysis_ctx(void) {
    AnalysisContext* ctx = malloc(sizeof(AnalysisContext));
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

void add_var(AnalysisContext* ctx, const char* name) {
    if (!ctx) return;
    VarUsage* v = malloc(sizeof(VarUsage));
    v->name = strdup(name);
    v->use_count = 0;
    v->last_use_depth = -1;
    v->escape_class = ESCAPE_NONE;
    v->captured_by_lambda = 0;
    v->freed = 0;
    v->next = ctx->vars;
    ctx->vars = v;
}

void record_use(AnalysisContext* ctx, const char* name) {
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

// -- Analysis Functions --

static void analyze_list(Value* list, AnalysisContext* ctx) {
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
            record_use(ctx, expr->s);
            break;

        case T_CELL: {
            Value* op = car(expr);
            Value* args = cdr(expr);

            if (op->tag == T_SYM) {
                if (strcmp(op->s, "quote") == 0) {
                    // Don't analyze quoted expressions
                } else if (strcmp(op->s, "lambda") == 0) {
                    int saved_in_lambda = ctx->in_lambda;
                    ctx->in_lambda = 1;
                    if (!is_nil(args) && !is_nil(cdr(args))) {
                        analyze_expr(car(cdr(args)), ctx);
                    }
                    ctx->in_lambda = saved_in_lambda;
                } else if (strcmp(op->s, "let") == 0) {
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));
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
                    analyze_expr(op, ctx);
                    analyze_list(args, ctx);
                }
            } else {
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
                    while (!is_nil(args)) {
                        analyze_escape(car(args), ctx, ESCAPE_ARG);
                        args = cdr(args);
                    }
                } else {
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

// -- Capture Tracking --

// Helper to check if variable is in bound list
static int is_bound(Value* bound, Value* sym) {
    while (!is_nil(bound)) {
        Value* pair = car(bound);
        if (pair->tag == T_CELL) {
            Value* key = car(pair);
            if (key->tag == T_SYM && sym->tag == T_SYM &&
                strcmp(key->s, sym->s) == 0) {
                return 1;
            }
        }
        bound = cdr(bound);
    }
    return 0;
}

// Helper to extend bound list
static Value* extend_bound(Value* bound, Value* sym) {
    return mk_cell(mk_cell(sym, mk_int(1)), bound);
}

void find_free_vars(Value* expr, Value* bound, char*** free_vars, int* count) {
    if (!expr || is_nil(expr)) return;

    if (expr->tag == T_SYM) {
        if (!is_bound(bound, expr)) {
            // Add to free vars if not already present
            for (int i = 0; i < *count; i++) {
                if (strcmp((*free_vars)[i], expr->s) == 0) return;
            }
            *free_vars = realloc(*free_vars, (*count + 1) * sizeof(char*));
            (*free_vars)[*count] = strdup(expr->s);
            (*count)++;
        }
        return;
    }

    if (expr->tag == T_CELL) {
        Value* op = car(expr);
        Value* args = cdr(expr);

        if (op->tag == T_SYM) {
            if (strcmp(op->s, "quote") == 0) {
                return;
            }
            if (strcmp(op->s, "lambda") == 0) {
                Value* params = car(args);
                Value* body = car(cdr(args));
                Value* new_bound = bound;
                while (!is_nil(params)) {
                    new_bound = extend_bound(new_bound, car(params));
                    params = cdr(params);
                }
                find_free_vars(body, new_bound, free_vars, count);
                return;
            }
            if (strcmp(op->s, "let") == 0) {
                Value* bindings = car(args);
                Value* body = car(cdr(args));
                Value* new_bound = bound;

                while (!is_nil(bindings)) {
                    Value* bind = car(bindings);
                    Value* sym = car(bind);
                    Value* val_expr = car(cdr(bind));

                    find_free_vars(val_expr, bound, free_vars, count);
                    new_bound = extend_bound(new_bound, sym);
                    bindings = cdr(bindings);
                }

                find_free_vars(body, new_bound, free_vars, count);
                return;
            }
        }

        // Recurse on all subexpressions
        find_free_vars(op, bound, free_vars, count);
        while (!is_nil(args)) {
            find_free_vars(car(args), bound, free_vars, count);
            args = cdr(args);
        }
    }
}
