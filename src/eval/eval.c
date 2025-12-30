#include "eval.h"
#include "../codegen/codegen.h"
#include "../analysis/escape.h"
#include "../analysis/shape.h"
#include <stdio.h>
#include <string.h>

// -- Symbol Table --

Value* NIL = NULL;
Value* SYM_T = NULL;
Value* SYM_QUOTE = NULL;
Value* SYM_IF = NULL;
Value* SYM_LAMBDA = NULL;
Value* SYM_LET = NULL;
Value* SYM_LIFT = NULL;
Value* SYM_RUN = NULL;
Value* SYM_EM = NULL;
Value* SYM_SCAN = NULL;
Value* SYM_GET_META = NULL;
Value* SYM_SET_META = NULL;

void init_syms(void) {
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
    return exp;
}

Value* h_var_default(Value* exp, Value* menv) {
    Value* v = env_lookup(menv->menv.env, exp);
    if (!v) {
        printf("Error: Unbound %s\n", exp->s);
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
        body_menv->menv.h_app = menv->menv.h_app;
        body_menv->menv.h_let = menv->menv.h_let;
        body_menv->menv.h_if = menv->menv.h_if;

        return eval(body, body_menv);
    }

    printf("Error: Not a function: %s\n", val_to_str(fn));
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
    Value* check_bindings = bindings;
    Value* new_env = menv->menv.env;

    BindingInfo* bind_list = NULL;
    BindingInfo* bind_tail = NULL;

    while (!is_nil(check_bindings)) {
        Value* bind = car(check_bindings);
        Value* sym = car(bind);
        Value* val_expr = car(cdr(bind));
        Value* val = eval(val_expr, menv);

        if (val->tag == T_CODE) any_code = 1;

        BindingInfo* info = malloc(sizeof(BindingInfo));
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
        AnalysisContext* ctx = mk_analysis_ctx();
        ShapeContext* shape_ctx = mk_shape_context();

        BindingInfo* b = bind_list;
        while (b) {
            add_var(ctx, b->sym->s);
            b = b->next;
        }

        analyze_expr(body, ctx);
        analyze_escape(body, ctx, ESCAPE_NONE);
        analyze_shapes_expr(exp, shape_ctx);

        char all_decls[8192] = "";
        char all_frees[4096] = "";
        b = bind_list;
        while (b) {
            VarUsage* usage = find_var(ctx, b->sym->s);
            int is_captured = usage ? usage->captured_by_lambda : 0;
            int use_count = usage ? usage->use_count : 0;

            ShapeInfo* shape_info = find_shape(shape_ctx, b->sym->s);
            Shape var_shape = shape_info ? shape_info->shape : SHAPE_UNKNOWN;

            char decl[1024];
            char* val_str = (b->val->tag == T_CODE) ? b->val->s : val_to_str(b->val);

            if (b->val->tag != T_CODE) {
                if (b->val->tag == T_INT) {
                    sprintf(decl, "  Obj* %s = mk_int(%ld);\n", b->sym->s, b->val->i);
                } else {
                    sprintf(decl, "  Obj* %s = %s;\n", b->sym->s, val_str);
                }
            } else {
                sprintf(decl, "  Obj* %s = %s;\n", b->sym->s, val_str);
            }
            strcat(all_decls, decl);

            const char* free_fn = shape_free_strategy(var_shape);

            if (!is_captured && use_count > 0) {
                char free_stmt[256];
                sprintf(free_stmt, "  %s(%s); // ASAP Clean (shape: %s)\n",
                        free_fn, b->sym->s,
                        var_shape == SHAPE_TREE ? "TREE" :
                        var_shape == SHAPE_DAG ? "DAG" :
                        var_shape == SHAPE_CYCLIC ? "CYCLIC" : "UNKNOWN");
                char temp[4096];
                strcpy(temp, free_stmt);
                strcat(temp, all_frees);
                strcpy(all_frees, temp);
            } else if (!is_captured && use_count == 0) {
                char free_stmt[256];
                sprintf(free_stmt, "  %s(%s); // unused\n", free_fn, b->sym->s);
                strcat(all_decls, free_stmt);
            } else if (is_captured) {
                char comment[256];
                sprintf(comment, "  // %s captured by closure - no free\n", b->sym->s);
                strcat(all_frees, comment);
            }

            Value* ref = mk_code(b->sym->s);
            new_env = env_extend(new_env, b->sym, ref);

            if (b->val->tag != T_CODE) free(val_str);
            b = b->next;
        }

        Value* body_menv = mk_menv(menv->menv.parent, new_env);
        body_menv->menv.h_app = menv->menv.h_app;
        body_menv->menv.h_let = menv->menv.h_let;

        Value* res = eval(body, body_menv);
        char* sres = (res->tag == T_CODE) ? res->s : val_to_str(res);

        char block[16384];
        sprintf(block, "({\n%s  Obj* _res = %s;\n%s  _res;\n})", all_decls, sres, all_frees);

        if (res->tag != T_CODE) free(sres);

        while (bind_list) {
            BindingInfo* next = bind_list->next;
            free(bind_list);
            bind_list = next;
        }

        return mk_code(block);
    }

    BindingInfo* b = bind_list;
    while (b) {
        new_env = env_extend(new_env, b->sym, b->val);
        b = b->next;
    }

    Value* body_menv = mk_menv(menv->menv.parent, new_env);
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
        char buf[2048];
        char* st = (t->tag == T_CODE) ? t->s : val_to_str(t);
        char* se = (e->tag == T_CODE) ? e->s : val_to_str(e);
        sprintf(buf, "(if %s %s %s)", c->s, st, se);
        if (t->tag != T_CODE) free(st);
        if (e->tag != T_CODE) free(se);
        return mk_code(buf);
    }

    if (!is_nil(c)) return eval(then_expr, menv);
    else return eval(else_expr, menv);
}

// -- Evaluator --

Value* eval(Value* expr, Value* menv) {
    if (is_nil(expr)) return NIL;
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
                menv->menv.parent = parent;
            }
            return eval(e, parent);
        }

        if (sym_eq(op, SYM_SET_META)) {
            Value* key = eval(car(args), menv);
            if (key->tag != T_SYM) key = car(args);
            Value* val = eval(car(cdr(args)), menv);
            if (sym_eq_str(key, "add")) {
                menv->menv.env = env_extend(menv->menv.env, mk_sym("+"), val);
            }
            return NIL;
        }

        if (sym_eq(op, SYM_SCAN)) {
            Value* type_sym = eval(car(args), menv);
            Value* val = eval(car(cdr(args)), menv);
            char buf[256];
            char* sval = (val->tag == T_CODE) ? val->s : val_to_str(val);
            sprintf(buf, "scan_%s(%s); // ASAP Mark", type_sym->s, sval);
            if (val->tag != T_CODE) free(sval);
            return mk_code(buf);
        }

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
    return mk_cell(a, b);
}

Value* prim_run(Value* args, Value* menv) {
    return eval(car(args), menv);
}
