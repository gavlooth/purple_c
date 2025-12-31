#include "eval.h"
#include "../codegen/codegen.h"
#include "../analysis/escape.h"
#include "../analysis/shape.h"
#include "../util/dstring.h"
#include <stdio.h>
#include <string.h>

// -- Symbol Table --

Value* NIL = NULL;
Value* SYM_T = NULL;
Value* SYM_QUOTE = NULL;
Value* SYM_IF = NULL;
Value* SYM_LAMBDA = NULL;
Value* SYM_LET = NULL;
Value* SYM_LETREC = NULL;
Value* SYM_AND = NULL;
Value* SYM_OR = NULL;
Value* SYM_LIFT = NULL;
Value* SYM_RUN = NULL;
Value* SYM_EM = NULL;
Value* SYM_SCAN = NULL;
Value* SYM_GET_META = NULL;
Value* SYM_SET_META = NULL;
static Value* SYM_UNINIT = NULL;

void init_syms(void) {
    NIL = alloc_val(T_NIL);
    SYM_T = mk_sym("t");
    SYM_QUOTE = mk_sym("quote");
    SYM_IF = mk_sym("if");
    SYM_LAMBDA = mk_sym("lambda");
    SYM_LET = mk_sym("let");
    SYM_LETREC = mk_sym("letrec");
    SYM_AND = mk_sym("and");
    SYM_OR = mk_sym("or");
    SYM_LIFT = mk_sym("lift");
    SYM_RUN = mk_sym("run");
    SYM_EM = mk_sym("EM");
    SYM_SCAN = mk_sym("scan");
    SYM_GET_META = mk_sym("get-meta");
    SYM_SET_META = mk_sym("set-meta!");
    SYM_UNINIT = alloc_val(T_PRIM);
    if (SYM_UNINIT) {
        SYM_UNINIT->prim = NULL;
    }
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
    return mk_cell(mk_cell(sym, val), env);
}

// -- MEnv --

Value* menv_env(Value* menv) {
    return menv->menv.env;
}

Value* menv_parent(Value* menv) {
    return menv->menv.parent;
}

Value* mk_menv(Value* parent, Value* env) {
    Value* v = alloc_val(T_MENV);
    if (!v) return NULL;
    v->menv.env = env;
    v->menv.parent = parent;
    v->menv.h_app = h_app_default;
    v->menv.h_let = h_let_default;
    v->menv.h_if = h_if_default;
    v->menv.h_lit = h_lit_default;
    v->menv.h_var = h_var_default;
    return v;
}

// -- Default Handlers --

Value* h_lit_default(Value* exp, Value* menv) {
    (void)menv;
    return exp;
}

Value* h_var_default(Value* exp, Value* menv) {
    Value* v = env_lookup(menv->menv.env, exp);
    if (!v) {
        printf("Error: Unbound %s\n", exp->s);
        return NIL;
    }
    if (v == SYM_UNINIT) {
        printf("Error: Uninitialized letrec binding %s\n", exp->s);
        return NIL;
    }
    return v;
}

Value* eval_list(Value* list, Value* menv) {
    if (is_nil(list)) return NIL;
    Value* h = eval(car(list), menv);
    Value* t = eval_list(cdr(list), menv);
    return mk_cell(h, t);
}

Value* h_app_default(Value* exp, Value* menv) {
    Value* f_expr = car(exp);
    Value* args_expr = cdr(exp);

    Value* fn = eval(f_expr, menv);
    if (!fn) return NIL;  // Guard: fn could be NULL
    Value* args = eval_list(args_expr, menv);

    if (fn->tag == T_PRIM) return fn->prim(args, menv);

    if (fn->tag == T_LAMBDA) {
        Value* params = fn->lam.params;
        Value* body = fn->lam.body;
        Value* closure_env = fn->lam.env;

        Value* new_env = closure_env;
        Value* p = params;
        Value* a = args;
        while (!is_nil(p) && !is_nil(a)) {
            new_env = env_extend(new_env, car(p), car(a));
            p = cdr(p);
            a = cdr(a);
        }

        Value* body_menv = mk_menv(menv->menv.parent, new_env);
        if (!body_menv) return NIL;
        body_menv->menv.h_app = menv->menv.h_app;
        body_menv->menv.h_let = menv->menv.h_let;
        body_menv->menv.h_if = menv->menv.h_if;

        return eval(body, body_menv);
    }

    char* fn_str = val_to_str(fn);
    printf("Error: Not a function: %s\n", fn_str ? fn_str : "(null)");
    free(fn_str);
    return NIL;
}

// Binding info for multi-let
typedef struct BindingInfo {
    Value* sym;
    Value* val;
    struct BindingInfo* next;
} BindingInfo;

Value* h_let_default(Value* exp, Value* menv) {
    Value* args = cdr(exp);
    Value* bindings = car(args);
    Value* body = car(cdr(args));

    int any_code = 0;
    int oom = 0;
    Value* check_bindings = bindings;
    Value* new_env = menv->menv.env;

    BindingInfo* bind_list = NULL;
    BindingInfo* bind_tail = NULL;

    while (!is_nil(check_bindings)) {
        Value* bind = car(check_bindings);
        Value* sym = car(bind);
        Value* val_expr = car(cdr(bind));
        Value* val = eval(val_expr, menv);
        if (!val) val = NIL;  // Guard against NULL from allocation failure

        if (val->tag == T_CODE) any_code = 1;

        BindingInfo* info = malloc(sizeof(BindingInfo));
        if (!info) {
            oom = 1;
            break;
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

    if (oom) {
        printf("Error: Out of memory while building let bindings\n");
        while (bind_list) {
            BindingInfo* next = bind_list->next;
            free(bind_list);
            bind_list = next;
        }
        return NIL;
    }

    if (any_code) {
        BindingInfo* b = bind_list;
        while (b) {
            if (b->val->tag != T_CODE) {
                char* tmp = val_to_c_expr(b->val);
                if (!tmp) {
                    any_code = 0;
                    break;
                }
                free(tmp);
            }
            b = b->next;
        }
    }

    if (any_code) {
        AnalysisContext* ctx = mk_analysis_ctx();
        ShapeContext* shape_ctx = mk_shape_context();

        BindingInfo* b = bind_list;
        while (b) {
            add_var(ctx, b->sym->s);
            b = b->next;
        }

        analyze_expr(body, ctx);
        analyze_escape(body, ctx, ESCAPE_GLOBAL);
        analyze_shapes_expr(exp, shape_ctx);

        DString* all_decls = ds_new();
        DString* all_frees = ds_new();
        b = bind_list;
        while (b) {
            VarUsage* usage = find_var(ctx, b->sym->s);
            int is_captured = usage ? usage->captured_by_lambda : 0;
            int use_count = usage ? usage->use_count : 0;
            int escape_class = usage ? usage->escape_class : ESCAPE_NONE;

            ShapeInfo* shape_info = find_shape(shape_ctx, b->sym->s);
            Shape var_shape = shape_info ? shape_info->shape : SHAPE_UNKNOWN;

            char* val_str = NULL;
            if (b->val->tag == T_CODE) {
                val_str = b->val->s;
            } else {
                val_str = val_to_c_expr(b->val);
                if (!val_str) {
                    printf("Error: cannot compile non-literal let binding for %s\n", b->sym->s);
                    break;
                }
            }

            if (b->val->tag != T_CODE) {
                if (b->val->tag == T_INT) {
                    ds_printf(all_decls, "  Obj* %s = mk_int(%ld);\n", b->sym->s, b->val->i);
                } else {
                    ds_printf(all_decls, "  Obj* %s = %s;\n", b->sym->s, val_str);
                }
            } else {
                ds_printf(all_decls, "  Obj* %s = %s;\n", b->sym->s, val_str);
            }

            const char* free_fn = shape_free_strategy(var_shape);

            if (is_captured) {
                ds_printf(all_frees, "  // %s captured by closure - no free\n", b->sym->s);
            } else if (use_count == 0) {
                ds_printf(all_decls, "  %s(%s); // unused\n", free_fn, b->sym->s);
            } else if (escape_class == ESCAPE_GLOBAL) {
                ds_printf(all_frees, "  // %s escapes to return - no free\n", b->sym->s);
            } else {
                DString* temp = ds_new();
                ds_printf(temp, "  %s(%s); // ASAP Clean (shape: %s)\n",
                        free_fn, b->sym->s,
                        var_shape == SHAPE_TREE ? "TREE" :
                        var_shape == SHAPE_DAG ? "DAG" :
                        var_shape == SHAPE_CYCLIC ? "CYCLIC" : "UNKNOWN");
                ds_append(temp, ds_cstr(all_frees));
                ds_free(all_frees);
                all_frees = temp;
            }

            Value* ref = mk_code(b->sym->s);
            new_env = env_extend(new_env, b->sym, ref);

            if (b->val->tag != T_CODE) free(val_str);
            b = b->next;
        }

        if (b) {
            ds_free(all_decls);
            ds_free(all_frees);
            free_analysis_ctx(ctx);
            free_shape_context(shape_ctx);
            while (bind_list) {
                BindingInfo* next = bind_list->next;
                free(bind_list);
                bind_list = next;
            }
            return NIL;
        }

        Value* body_menv = mk_menv(menv->menv.parent, new_env);
        if (!body_menv) {
            ds_free(all_decls);
            ds_free(all_frees);
            free_analysis_ctx(ctx);
            free_shape_context(shape_ctx);
            while (bind_list) {
                BindingInfo* next = bind_list->next;
                free(bind_list);
                bind_list = next;
            }
            return NIL;
        }
        body_menv->menv.h_app = menv->menv.h_app;
        body_menv->menv.h_let = menv->menv.h_let;

        Value* res = eval(body, body_menv);
        int sres_owned = (!res || res->tag != T_CODE);
        char* sres = (res && res->tag == T_CODE) ? res->s : val_to_str(res);

        DString* block = ds_new();
        ds_printf(block, "({\n%s  Obj* _res = %s;\n%s  _res;\n})",
                  ds_cstr(all_decls), sres ? sres : "NULL", ds_cstr(all_frees));

        if (sres_owned) free(sres);

        ds_free(all_decls);
        ds_free(all_frees);
        free_analysis_ctx(ctx);
        free_shape_context(shape_ctx);

        while (bind_list) {
            BindingInfo* next = bind_list->next;
            free(bind_list);
            bind_list = next;
        }

        char* code_str = ds_take(block);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
    }

    BindingInfo* b = bind_list;
    while (b) {
        new_env = env_extend(new_env, b->sym, b->val);
        b = b->next;
    }

    Value* body_menv = mk_menv(menv->menv.parent, new_env);
    if (!body_menv) {
        while (bind_list) {
            BindingInfo* next = bind_list->next;
            free(bind_list);
            bind_list = next;
        }
        return NIL;
    }
    body_menv->menv.h_app = menv->menv.h_app;
    body_menv->menv.h_let = menv->menv.h_let;

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
        int st_owned = (!t || t->tag != T_CODE);
        int se_owned = (!e || e->tag != T_CODE);
        char* st = (t && t->tag == T_CODE) ? t->s : val_to_str(t);
        char* se = (e && e->tag == T_CODE) ? e->s : val_to_str(e);
        DString* ds = ds_new();
        ds_printf(ds, "((%s)->i ? (%s) : (%s))", c->s, st ? st : "NULL", se ? se : "NULL");
        if (st_owned) free(st);
        if (se_owned) free(se);
        char* code_str = ds_take(ds);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
    }

    if (!is_nil(c)) return eval(then_expr, menv);
    else return eval(else_expr, menv);
}

// -- Evaluator --

Value* eval(Value* expr, Value* menv) {
    if (is_nil(expr)) return NIL;
    if (!menv) return NIL;  // NULL check for menv
    if (expr->tag == T_INT) return menv->menv.h_lit(expr, menv);
    if (expr->tag == T_CODE) return expr;

    if (expr->tag == T_SYM) {
        return menv->menv.h_var(expr, menv);
    }

    if (expr->tag == T_CELL) {
        Value* op = car(expr);
        Value* args = cdr(expr);

        if (sym_eq(op, SYM_QUOTE)) return car(args);
        if (sym_eq(op, SYM_LIFT)) return lift_value(eval(car(args), menv));

        if (sym_eq(op, SYM_IF)) return menv->menv.h_if(expr, menv);
        if (sym_eq(op, SYM_LET)) return menv->menv.h_let(expr, menv);

        // letrec - recursive let binding
        if (sym_eq(op, SYM_LETREC)) {
            Value* bindings = car(args);
            Value* body = car(cdr(args));

            // First pass: extend env with placeholders
            Value* new_env = menv->menv.env;
            Value* b = bindings;
        while (!is_nil(b)) {
            Value* bind = car(b);
            Value* sym = car(bind);
            new_env = env_extend(new_env, sym, SYM_UNINIT);  // Placeholder
            b = cdr(b);
        }

            // Create new menv for evaluating bindings
            Value* rec_menv = mk_menv(menv->menv.parent, new_env);
            if (!rec_menv) return NIL;
            rec_menv->menv.h_app = menv->menv.h_app;
            rec_menv->menv.h_let = menv->menv.h_let;
            rec_menv->menv.h_if = menv->menv.h_if;

            // Second pass: evaluate and update bindings
            b = bindings;
            while (!is_nil(b)) {
                Value* bind = car(b);
                Value* val_expr = car(cdr(bind));
                Value* val = eval(val_expr, rec_menv);

                // Update the placeholder in environment
                // Find and update the binding
                Value* e = new_env;
                Value* sym = car(bind);
                while (!is_nil(e)) {
                    Value* pair = car(e);
                    if (sym_eq(car(pair), sym)) {
                        pair->cell.cdr = val;
                        break;
                    }
                    e = cdr(e);
                }
                b = cdr(b);
            }

            return eval(body, rec_menv);
        }

        // Short-circuit and
        if (sym_eq(op, SYM_AND)) {
            Value* rest = args;
            Value* result = SYM_T;
            while (!is_nil(rest)) {
                result = eval(car(rest), menv);
                if (is_code(result)) {
                    // At code level, generate && chain
                    Value* remaining = cdr(rest);
                    while (!is_nil(remaining)) {
                        Value* next = eval(car(remaining), menv);
                        char* sr = result->s;
                        char* sn = is_code(next) ? next->s : val_to_str(next);
                        DString* ds = ds_new();
                        ds_printf(ds, "(%s && %s)", sr, sn);
                        if (!is_code(next)) free(sn);
                        char* code_str = ds_take(ds);
                        result = mk_code(code_str);
                        free(code_str);
                        remaining = cdr(remaining);
                    }
                    return result;
                }
                if (is_nil(result)) return NIL;
                rest = cdr(rest);
            }
            return result;
        }

        // Short-circuit or
        if (sym_eq(op, SYM_OR)) {
            Value* rest = args;
            while (!is_nil(rest)) {
                Value* result = eval(car(rest), menv);
                if (is_code(result)) {
                    // At code level, generate || chain
                    Value* remaining = cdr(rest);
                    while (!is_nil(remaining)) {
                        Value* next = eval(car(remaining), menv);
                        char* sr = result->s;
                        char* sn = is_code(next) ? next->s : val_to_str(next);
                        DString* ds = ds_new();
                        ds_printf(ds, "(%s || %s)", sr, sn);
                        if (!is_code(next)) free(sn);
                        char* code_str = ds_take(ds);
                        result = mk_code(code_str);
                        free(code_str);
                        remaining = cdr(remaining);
                    }
                    return result;
                }
                if (!is_nil(result)) return result;
                rest = cdr(rest);
            }
            return NIL;
        }

        if (sym_eq(op, SYM_LAMBDA)) {
            Value* params = car(args);
            Value* body = car(cdr(args));
            return mk_lambda(params, body, menv->menv.env);
        }

        if (sym_eq(op, SYM_EM)) {
            Value* e = car(args);
            Value* parent = menv->menv.parent;
            if (is_nil(parent)) {
                parent = mk_menv(NIL, NIL);
                if (!parent) return NIL;
                menv->menv.parent = parent;
            }
            return eval(e, parent);
        }

        if (sym_eq(op, SYM_SET_META)) {
            Value* key = eval(car(args), menv);
            if (!key || key->tag != T_SYM) key = car(args);
            Value* val = eval(car(cdr(args)), menv);
            if (sym_eq_str(key, "add")) {
                menv->menv.env = env_extend(menv->menv.env, mk_sym("+"), val);
            }
            return NIL;
        }

        if (sym_eq(op, SYM_SCAN)) {
            Value* type_sym = eval(car(args), menv);
            Value* val = eval(car(cdr(args)), menv);
            if (!type_sym || type_sym->tag != T_SYM || !type_sym->s) return NIL;
            if (!val) return NIL;
            char* sval = (val->tag == T_CODE) ? val->s : val_to_str(val);
            if (!sval) return NIL;
            DString* ds = ds_new();
            ds_printf(ds, "scan_%s(%s); // ASAP Mark", type_sym->s, sval);
            if (val->tag != T_CODE) free(sval);
            char* code_str = ds_take(ds);
            Value* result = mk_code(code_str);
            free(code_str);
            return result;
        }

        return menv->menv.h_app(expr, menv);
    }
    return NIL;
}

// -- Primitives --

// Helper: safely get two int args, return 0 on error
static int get_two_args(Value* args, Value** a, Value** b) {
    if (!args || is_nil(args)) return 0;
    *a = car(args);
    if (!*a) return 0;
    Value* rest = cdr(args);
    if (!rest || is_nil(rest)) return 0;
    *b = car(rest);
    if (!*b) return 0;
    return 1;
}

Value* prim_add(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("add", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return mk_int(a->i + b->i);
}

Value* prim_sub(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("sub", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return mk_int(a->i - b->i);
}

Value* prim_cons(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("mk_pair", a, b);
    return mk_cell(a, b);
}

Value* prim_run(Value* args, Value* menv) {
    Value* a = car(args);
    if (!a) return NIL;
    return eval(a, menv);
}

// -- Additional Arithmetic Primitives --

Value* prim_mul(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("mul", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return mk_int(a->i * b->i);
}

Value* prim_div(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("div_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    if (b->i == 0) return mk_int(0);
    return mk_int(a->i / b->i);
}

Value* prim_mod(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("mod_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    if (b->i == 0) return mk_int(0);
    return mk_int(a->i % b->i);
}

// -- Comparison Primitives --

Value* prim_eq(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("eq_op", a, b);
    // Handle different types
    if (a->tag == T_INT && b->tag == T_INT) {
        return a->i == b->i ? SYM_T : NIL;
    }
    if (a->tag == T_SYM && b->tag == T_SYM) {
        return sym_eq(a, b) ? SYM_T : NIL;
    }
    if (a->tag == T_NIL && b->tag == T_NIL) return SYM_T;
    return NIL;
}

Value* prim_lt(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("lt_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return a->i < b->i ? SYM_T : NIL;
}

Value* prim_gt(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("gt_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return a->i > b->i ? SYM_T : NIL;
}

Value* prim_le(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("le_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return a->i <= b->i ? SYM_T : NIL;
}

Value* prim_ge(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("ge_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    return a->i >= b->i ? SYM_T : NIL;
}

// -- Logical Primitives --

Value* prim_not(Value* args, Value* menv) {
    (void)menv;
    if (!args || is_nil(args)) return SYM_T;  // not of nothing is true
    Value* a = car(args);
    if (!a) return SYM_T;
    if (is_code(a)) return emit_c_call("not_op", a, NIL);
    return is_nil(a) ? SYM_T : NIL;
}

// -- List Primitives --

// Helper: safely get one arg
static Value* get_one_arg(Value* args) {
    if (!args || is_nil(args)) return NULL;
    return car(args);
}

Value* prim_car(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (!a) return NIL;
    if (is_code(a)) {
        DString* ds = ds_new();
        ds_printf(ds, "(%s)->a", a->s);
        char* code_str = ds_take(ds);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
    }
    if (a->tag != T_CELL) return NIL;
    return car(a);
}

Value* prim_cdr(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (!a) return NIL;
    if (is_code(a)) {
        DString* ds = ds_new();
        ds_printf(ds, "(%s)->b", a->s);
        char* code_str = ds_take(ds);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
    }
    if (a->tag != T_CELL) return NIL;
    return cdr(a);
}

// fst and snd are aliases for car and cdr
Value* prim_fst(Value* args, Value* menv) {
    return prim_car(args, menv);
}

Value* prim_snd(Value* args, Value* menv) {
    return prim_cdr(args, menv);
}

Value* prim_null(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (!a) return SYM_T;  // null? of nothing is true
    if (is_code(a)) {
        DString* ds = ds_new();
        ds_printf(ds, "is_nil(%s)", a->s);
        char* code_str = ds_take(ds);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
    }
    return is_nil(a) ? SYM_T : NIL;
}
