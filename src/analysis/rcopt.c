/*
 * RC Optimization Module (Lobster-style)
 *
 * Implements compile-time reference counting optimization:
 * - Alias tracking: When two variables point to the same object
 * - Uniqueness analysis: Prove when an object has exactly one reference
 * - Borrow tracking: Parameters and temporary references without ownership
 *
 * Based on research from:
 * - Lobster language compile-time RC (95% RC ops eliminated)
 * - ASAP memory management
 */

#define _POSIX_C_SOURCE 200809L
#include "rcopt.h"
#include "shape.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>

/* Create a new RC optimization context */
RCOptContext* mk_rcopt_context(void) {
    RCOptContext* ctx = malloc(sizeof(RCOptContext));
    if (!ctx) return NULL;
    ctx->vars = NULL;
    ctx->current_point = 0;
    ctx->eliminated = 0;
    return ctx;
}

/* Free RC optimization context */
void free_rcopt_context(RCOptContext* ctx) {
    if (!ctx) return;
    RCOptInfo* info = ctx->vars;
    while (info) {
        RCOptInfo* next = info->next;
        free(info->var_name);
        free(info->alias_of);
        for (int i = 0; i < info->alias_count; i++) {
            free(info->aliases[i]);
        }
        free(info->aliases);
        free(info);
        info = next;
    }
    free(ctx);
}

/* Advance program point */
static int next_point(RCOptContext* ctx) {
    ctx->current_point++;
    return ctx->current_point;
}

/* Find variable info */
RCOptInfo* rcopt_find_var(RCOptContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;
    RCOptInfo* info = ctx->vars;
    while (info) {
        if (strcmp(info->var_name, name) == 0) {
            return info;
        }
        info = info->next;
    }
    return NULL;
}

/* Add alias to variable */
static void add_alias(RCOptInfo* info, const char* alias) {
    if (!info || !alias) return;

    if (info->alias_count >= info->alias_capacity) {
        int new_cap;
        if (info->alias_capacity == 0) {
            new_cap = 4;
        } else if (info->alias_capacity > INT_MAX / 2) {
            return;  /* Overflow protection */
        } else {
            new_cap = info->alias_capacity * 2;
        }
        char** new_aliases = realloc(info->aliases, new_cap * sizeof(char*));
        if (!new_aliases) return;
        info->aliases = new_aliases;
        info->alias_capacity = new_cap;
    }

    info->aliases[info->alias_count] = strdup(alias);
    if (info->aliases[info->alias_count]) {
        info->alias_count++;
    }
}

/* Define a new variable from a fresh allocation (unique) */
RCOptInfo* rcopt_define_var(RCOptContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;

    RCOptInfo* info = malloc(sizeof(RCOptInfo));
    if (!info) return NULL;

    info->var_name = strdup(name);
    if (!info->var_name) {
        free(info);
        return NULL;
    }
    info->is_unique = 1;  /* Fresh allocations are unique */
    info->is_borrowed = 0;
    info->defined_at = next_point(ctx);
    info->last_used_at = 0;
    info->alias_of = NULL;
    info->aliases = NULL;
    info->alias_count = 0;
    info->alias_capacity = 0;

    info->next = ctx->vars;
    ctx->vars = info;

    return info;
}

/* Define a variable as an alias of another */
RCOptInfo* rcopt_define_alias(RCOptContext* ctx, const char* name, const char* alias_of) {
    if (!ctx || !name || !alias_of) return NULL;

    RCOptInfo* original = rcopt_find_var(ctx, alias_of);
    if (!original) {
        /* Unknown original, be conservative */
        return rcopt_define_var(ctx, name);
    }

    /* The original is no longer unique */
    original->is_unique = 0;

    RCOptInfo* info = malloc(sizeof(RCOptInfo));
    if (!info) return NULL;

    info->var_name = strdup(name);
    if (!info->var_name) {
        free(info);
        return NULL;
    }
    info->is_unique = 0;
    info->is_borrowed = 0;
    info->defined_at = next_point(ctx);
    info->last_used_at = 0;
    info->alias_of = strdup(alias_of);
    if (!info->alias_of) {
        free(info->var_name);
        free(info);
        return NULL;
    }
    info->aliases = NULL;
    info->alias_count = 0;
    info->alias_capacity = 0;

    /* Add to each other's alias list */
    add_alias(original, name);
    add_alias(info, alias_of);

    info->next = ctx->vars;
    ctx->vars = info;

    return info;
}

/* Define a borrowed reference (no RC ops needed) */
RCOptInfo* rcopt_define_borrowed(RCOptContext* ctx, const char* name) {
    if (!ctx || !name) return NULL;

    RCOptInfo* info = malloc(sizeof(RCOptInfo));
    if (!info) return NULL;

    info->var_name = strdup(name);
    if (!info->var_name) {
        free(info);
        return NULL;
    }
    info->is_unique = 0;
    info->is_borrowed = 1;
    info->defined_at = next_point(ctx);
    info->last_used_at = 0;
    info->alias_of = NULL;
    info->aliases = NULL;
    info->alias_count = 0;
    info->alias_capacity = 0;

    info->next = ctx->vars;
    ctx->vars = info;

    return info;
}

/* Mark a variable as used */
void rcopt_mark_used(RCOptContext* ctx, const char* name) {
    if (!ctx || !name) return;
    RCOptInfo* info = rcopt_find_var(ctx, name);
    if (info) {
        info->last_used_at = next_point(ctx);
    }
}

/* Get optimized inc_ref strategy */
RCOptimization rcopt_get_inc_ref(RCOptContext* ctx, const char* name) {
    if (!ctx || !name) return RC_OPT_NONE;

    RCOptInfo* info = rcopt_find_var(ctx, name);
    if (!info) return RC_OPT_NONE;

    /* Borrowed references don't need inc_ref */
    if (info->is_borrowed) {
        ctx->eliminated++;
        return RC_OPT_ELIDE_INC;
    }

    /* If this is an alias and original will handle RC, skip */
    if (info->alias_of) {
        RCOptInfo* original = rcopt_find_var(ctx, info->alias_of);
        if (original && !original->is_borrowed) {
            ctx->eliminated++;
            return RC_OPT_ELIDE_INC;
        }
    }

    return RC_OPT_NONE;
}

/* Get optimized dec_ref strategy */
RCOptimization rcopt_get_dec_ref(RCOptContext* ctx, const char* name) {
    if (!ctx || !name) return RC_OPT_NONE;

    RCOptInfo* info = rcopt_find_var(ctx, name);
    if (!info) return RC_OPT_NONE;

    /* Borrowed references don't need dec_ref */
    if (info->is_borrowed) {
        ctx->eliminated++;
        return RC_OPT_ELIDE_DEC;
    }

    /* If this has aliases, check if another alias will dec_ref */
    for (int i = 0; i < info->alias_count; i++) {
        RCOptInfo* alias = rcopt_find_var(ctx, info->aliases[i]);
        if (alias && alias->last_used_at > info->last_used_at) {
            /* Another alias is used later, it will handle dec_ref */
            ctx->eliminated++;
            return RC_OPT_ELIDE_DEC;
        }
    }

    /* If proven unique, can use direct free */
    if (info->is_unique) {
        ctx->eliminated++;
        return RC_OPT_DIRECT_FREE;
    }

    return RC_OPT_NONE;
}

/* Get the best free function to use */
const char* rcopt_get_free_function(RCOptContext* ctx, const char* name, int shape) {
    if (!ctx || !name) return shape_free_strategy(shape);

    RCOptimization opt = rcopt_get_dec_ref(ctx, name);

    switch (opt) {
        case RC_OPT_DIRECT_FREE:
            /* Proven unique at compile time - no RC check needed */
            return "free_unique";
        case RC_OPT_ELIDE_DEC:
            /* Skip the dec_ref entirely */
            return NULL;
        default:
            /* Use shape-based strategy */
            return shape_free_strategy(shape);
    }
}

/* Analyze expression for RC optimization */
void rcopt_analyze_expr(RCOptContext* ctx, Value* expr) {
    if (!ctx || !expr || is_nil(expr)) return;

    switch (expr->tag) {
        case T_INT:
        case T_NIL:
            /* Literals - no RC tracking needed */
            break;

        case T_SYM:
            /* Variable use */
            rcopt_mark_used(ctx, expr->s);
            break;

        case T_CELL: {
            Value* op = car(expr);
            Value* args = cdr(expr);

            if (op && op->tag == T_SYM) {
                /* LET binding */
                if (strcmp(op->s, "let") == 0) {
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));

                    /* Process bindings */
                    while (!is_nil(bindings) && bindings->tag == T_CELL) {
                        Value* bind = car(bindings);
                        Value* sym = car(bind);
                        Value* val_expr = car(cdr(bind));

                        /* Analyze value expression first */
                        rcopt_analyze_expr(ctx, val_expr);

                        /* Define variable */
                        if (sym && sym->tag == T_SYM) {
                            if (val_expr && val_expr->tag == T_SYM) {
                                /* Value is a variable - creates alias */
                                rcopt_define_alias(ctx, sym->s, val_expr->s);
                            } else {
                                /* Fresh allocation */
                                rcopt_define_var(ctx, sym->s);
                            }
                        }

                        bindings = cdr(bindings);
                    }

                    /* Analyze body */
                    rcopt_analyze_expr(ctx, body);
                    return;
                }

                /* SET! */
                if (strcmp(op->s, "set!") == 0) {
                    Value* target = car(args);
                    Value* value = car(cdr(args));

                    rcopt_analyze_expr(ctx, value);

                    /* set! creates an alias */
                    if (target && target->tag == T_SYM && value && value->tag == T_SYM) {
                        rcopt_define_alias(ctx, target->s, value->s);
                    }
                    return;
                }

                /* LAMBDA */
                if (strcmp(op->s, "lambda") == 0) {
                    Value* params = car(args);
                    Value* body = car(cdr(args));

                    /* Parameters are borrowed */
                    while (!is_nil(params) && params->tag == T_CELL) {
                        Value* param = car(params);
                        if (param && param->tag == T_SYM) {
                            rcopt_define_borrowed(ctx, param->s);
                        }
                        params = cdr(params);
                    }

                    /* Analyze body */
                    rcopt_analyze_expr(ctx, body);
                    return;
                }
            }

            /* Default: analyze all subexpressions */
            rcopt_analyze_expr(ctx, op);
            while (!is_nil(args) && args->tag == T_CELL) {
                rcopt_analyze_expr(ctx, car(args));
                args = cdr(args);
            }
            break;
        }

        default:
            break;
    }
}

/* Get statistics */
void rcopt_get_stats(RCOptContext* ctx, int* total, int* eliminated) {
    if (!ctx) {
        if (total) *total = 0;
        if (eliminated) *eliminated = 0;
        return;
    }

    /* Count total variables (assume 2 RC ops per var: inc + dec) */
    int count = 0;
    RCOptInfo* info = ctx->vars;
    while (info) {
        count++;
        info = info->next;
    }

    if (total) *total = count * 2;
    if (eliminated) *eliminated = ctx->eliminated;
}

/* Convert optimization to string */
const char* rcopt_string(RCOptimization opt) {
    switch (opt) {
        case RC_OPT_ELIDE_INC: return "elide_inc_ref";
        case RC_OPT_ELIDE_DEC: return "elide_dec_ref";
        case RC_OPT_DIRECT_FREE: return "direct_free";
        case RC_OPT_BATCHED_FREE: return "batched_free";
        case RC_OPT_ELIDE_ALL: return "elide_all";
        default: return "none";
    }
}
