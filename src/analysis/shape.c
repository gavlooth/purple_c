#include "shape.h"

// Shape lattice: TREE < DAG < CYCLIC (join = max)
Shape shape_join(Shape a, Shape b) {
    if (a == SHAPE_CYCLIC || b == SHAPE_CYCLIC) return SHAPE_CYCLIC;
    if (a == SHAPE_DAG || b == SHAPE_DAG) return SHAPE_DAG;
    if (a == SHAPE_TREE || b == SHAPE_TREE) return SHAPE_TREE;
    return SHAPE_UNKNOWN;
}

const char* shape_to_string(Shape s) {
    switch (s) {
        case SHAPE_TREE: return "TREE";
        case SHAPE_DAG: return "DAG";
        case SHAPE_CYCLIC: return "CYCLIC";
        default: return "UNKNOWN";
    }
}

ShapeContext* mk_shape_context(void) {
    ShapeContext* ctx = malloc(sizeof(ShapeContext));
    if (!ctx) return NULL;
    ctx->shapes = NULL;
    ctx->changed = 0;
    ctx->next_alias_group = 1;
    ctx->result_shape = SHAPE_UNKNOWN;
    return ctx;
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
                        add_shape(ctx, sym->s, ctx->result_shape);

                        bindings = cdr(bindings);
                    }

                    analyze_shapes_expr(body, ctx);
                    return;
                }

                // LETREC binding (potentially cyclic)
                if (strcmp(op->s, "letrec") == 0) {
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));

                    // Pre-mark all bound symbols as cyclic
                    Value* b = bindings;
                    while (!is_nil(b)) {
                        Value* bind = car(b);
                        Value* sym = car(bind);
                        add_shape(ctx, sym->s, SHAPE_CYCLIC);
                        b = cdr(b);
                    }

                    // Analyze binding expressions (will join with CYCLIC)
                    b = bindings;
                    while (!is_nil(b)) {
                        Value* bind = car(b);
                        Value* sym = car(bind);
                        Value* val_expr = car(cdr(bind));

                        analyze_shapes_expr(val_expr, ctx);
                        add_shape(ctx, sym->s, ctx->result_shape);

                        b = cdr(b);
                    }

                    analyze_shapes_expr(body, ctx);
                    return;
                }

                // SET! can create cycles
                if (strcmp(op->s, "set!") == 0) {
                    Value* target = car(args);
                    if (target->tag == T_SYM) {
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

                // LAMBDA - closure is TREE
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

            // Default: analyze all subexpressions and join their shapes
            Shape result = SHAPE_UNKNOWN;
            analyze_shapes_expr(op, ctx);
            result = shape_join(result, ctx->result_shape);
            while (!is_nil(args)) {
                analyze_shapes_expr(car(args), ctx);
                result = shape_join(result, ctx->result_shape);
                args = cdr(args);
            }
            // Default to DAG if still unknown (conservative)
            ctx->result_shape = (result == SHAPE_UNKNOWN) ? SHAPE_DAG : result;
            break;
        }

        default:
            ctx->result_shape = SHAPE_UNKNOWN;
            break;
    }
}

const char* shape_free_strategy(Shape s) {
    switch (s) {
        case SHAPE_TREE: return "free_tree";
        case SHAPE_DAG: return "dec_ref";
        case SHAPE_CYCLIC: return "deferred_release";
        // SHAPE_UNKNOWN defaults to dec_ref (safe for both trees and DAGs)
        // Using free_tree for unknown shapes risks infinite loops on cycles
        default: return "dec_ref";
    }
}
