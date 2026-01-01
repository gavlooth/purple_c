#include "eval.h"
#include "../codegen/codegen.h"
#include "../analysis/escape.h"
#include "../analysis/shape.h"
#include "../util/dstring.h"
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>

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
Value* SYM_SET_BANG = NULL;
Value* SYM_DEFINE = NULL;
Value* SYM_DO = NULL;
Value* SYM_CALL_CC = NULL;
Value* SYM_PROMPT = NULL;
Value* SYM_CONTROL = NULL;
Value* SYM_GO = NULL;
Value* SYM_SELECT = NULL;
static Value* SYM_UNINIT = NULL;

// -- Global Environment --
static Value* global_env = NULL;

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
    SYM_SET_BANG = mk_sym("set!");
    SYM_DEFINE = mk_sym("define");
    SYM_DO = mk_sym("do");
    SYM_CALL_CC = mk_sym("call/cc");
    SYM_PROMPT = mk_sym("prompt");
    SYM_CONTROL = mk_sym("control");
    SYM_GO = mk_sym("go");
    SYM_SELECT = mk_sym("select");
    SYM_UNINIT = alloc_val(T_PRIM);
    if (SYM_UNINIT) {
        SYM_UNINIT->prim = NULL;
    }
    global_env = NIL;  // Initialize global environment
}

// -- Global Environment Functions --

void global_define(Value* sym, Value* val) {
    if (!sym || sym->tag != T_SYM) return;

    // Check if already defined, update if so
    Value* env = global_env;
    while (!is_nil(env)) {
        Value* pair = car(env);
        if (pair && sym_eq(car(pair), sym)) {
            // Update existing binding
            pair->cell.cdr = val;
            return;
        }
        env = cdr(env);
    }
    // Not found, prepend new binding
    global_env = mk_cell(mk_cell(sym, val), global_env);
}

Value* global_lookup(Value* sym) {
    if (!sym || sym->tag != T_SYM) return NULL;
    return env_lookup(global_env, sym);
}

int env_set(Value* env, Value* sym, Value* val) {
    while (!is_nil(env)) {
        Value* pair = car(env);
        if (pair && sym_eq(car(pair), sym)) {
            // Mutate the binding in place
            pair->cell.cdr = val;
            return 1;  // Found and set
        }
        env = cdr(env);
    }
    return 0;  // Not found
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
    // First check local environment
    Value* v = env_lookup(menv->menv.env, exp);
    if (v) {
        if (v == SYM_UNINIT) {
            printf("Error: Uninitialized letrec binding %s\n", exp->s);
            return NIL;
        }
        return v;
    }
    // Fall back to global environment
    v = global_lookup(exp);
    if (v) {
        return v;
    }
    printf("Error: Unbound %s\n", exp->s);
    return NIL;
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

    // Handle continuation invocation
    if (fn->tag == T_CONT) {
        Value* arg = is_nil(args) ? NIL : car(args);
        return invoke_continuation(fn, arg);
    }

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
        if (!sym || sym->tag != T_SYM || !sym->s) {
            check_bindings = cdr(check_bindings);
            continue;  // Skip malformed binding
        }
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

// Helper: Check if a code string is a simple variable name (identifier)
// Simple names don't need dec_ref since they're managed by their enclosing scope
static int is_simple_var_name(const char* s) {
    if (!s || !*s) return 0;
    // First char must be letter or underscore
    if (!isalpha((unsigned char)s[0]) && s[0] != '_') return 0;
    // Rest must be alphanumeric or underscore
    for (const char* p = s + 1; *p; p++) {
        if (!isalnum((unsigned char)*p) && *p != '_') return 0;
    }
    return 1;
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
        // Use a block expression that stores condition in temp variable
        // to avoid memory leak from evaluating the condition
        // Check for NULL before dereferencing to handle OOM in condition
        // Don't dec_ref if condition is a simple variable name - it's managed by its scope
        if (is_simple_var_name(c->s)) {
            // Simple variable reference - no dec_ref needed (scope manages it)
            ds_printf(ds, "({ Obj* _cond = %s; Obj* _r = (_cond && _cond->i) ? (%s) : (%s); _r; })",
                      c->s, st ? st : "NULL", se ? se : "NULL");
        } else {
            // Complex expression - may allocate, so dec_ref after use
            ds_printf(ds, "({ Obj* _cond = %s; Obj* _r = (_cond && _cond->i) ? (%s) : (%s); if (_cond) dec_ref(_cond); _r; })",
                      c->s, st ? st : "NULL", se ? se : "NULL");
        }
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
                        if (!sn) sn = strdup("NULL");
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
                        if (!sn) sn = strdup("NULL");
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

        // set! - mutate existing variable binding
        if (sym_eq(op, SYM_SET_BANG)) {
            Value* var_sym = car(args);
            if (!var_sym || var_sym->tag != T_SYM) {
                return mk_error("set!: first argument must be a symbol");
            }
            Value* new_val = eval(car(cdr(args)), menv);
            // Try local env first
            if (env_set(menv->menv.env, var_sym, new_val)) {
                return new_val;
            }
            // Try global env
            if (env_set(global_env, var_sym, new_val)) {
                return new_val;
            }
            printf("Error: set!: unbound variable %s\n", var_sym->s);
            return mk_error("set!: unbound variable");
        }

        // define - create global binding
        if (sym_eq(op, SYM_DEFINE)) {
            Value* first = car(args);
            // Case 1: (define (name args...) body) - function shorthand
            if (first && first->tag == T_CELL) {
                Value* name = car(first);
                if (!name || name->tag != T_SYM) {
                    return mk_error("define: function name must be a symbol");
                }
                Value* params = cdr(first);
                Value* body = car(cdr(args));
                Value* lam = mk_lambda(params, body, menv->menv.env);
                global_define(name, lam);
                return name;
            }
            // Case 2: (define name value)
            if (!first || first->tag != T_SYM) {
                return mk_error("define: first argument must be a symbol or (name args...)");
            }
            if (is_nil(cdr(args))) {
                return mk_error("define: requires a value");
            }
            Value* val = eval(car(cdr(args)), menv);
            global_define(first, val);
            return first;
        }

        // do - evaluate sequence, return last result
        if (sym_eq(op, SYM_DO)) {
            Value* result = NIL;
            Value* rest = args;
            while (!is_nil(rest)) {
                result = eval(car(rest), menv);
                rest = cdr(rest);
            }
            return result;
        }

        // call/cc - call with current continuation
        if (sym_eq(op, SYM_CALL_CC)) {
            return eval_call_cc(args, menv);
        }

        // prompt - establish delimiter for control
        if (sym_eq(op, SYM_PROMPT)) {
            return eval_prompt(args, menv);
        }

        // control - capture delimited continuation
        if (sym_eq(op, SYM_CONTROL)) {
            return eval_control(args, menv);
        }

        // go - spawn green thread
        if (sym_eq(op, SYM_GO)) {
            return eval_go(args, menv);
        }

        // select - wait on multiple channels
        if (sym_eq(op, SYM_SELECT)) {
            return eval_select(args, menv);
        }

        // deftype - define user type
        if (sym_eq_str(op, "deftype")) {
            return eval_deftype(args, menv);
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
    // Overflow protection: return 0 on overflow (consistent with generated code)
    if ((b->i > 0 && a->i > LONG_MAX - b->i) ||
        (b->i < 0 && a->i < LONG_MIN - b->i)) {
        return mk_int(0);
    }
    return mk_int(a->i + b->i);
}

Value* prim_sub(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("sub", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    // Overflow protection: return 0 on overflow (consistent with generated code)
    if ((b->i < 0 && a->i > LONG_MAX + b->i) ||
        (b->i > 0 && a->i < LONG_MIN + b->i)) {
        return mk_int(0);
    }
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
    // Overflow protection: return 0 on overflow (consistent with generated code)
    if (a->i > 0 && b->i > 0 && a->i > LONG_MAX / b->i) return mk_int(0);
    if (a->i > 0 && b->i < 0 && b->i < LONG_MIN / a->i) return mk_int(0);
    if (a->i < 0 && b->i > 0 && a->i < LONG_MIN / b->i) return mk_int(0);
    if (a->i < 0 && b->i < 0 && a->i < LONG_MAX / b->i) return mk_int(0);
    return mk_int(a->i * b->i);
}

Value* prim_div(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("div_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    if (b->i == 0 || (a->i == LONG_MIN && b->i == -1)) return mk_int(0);
    return mk_int(a->i / b->i);
}

Value* prim_mod(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) return NIL;
    if (is_code(a) || is_code(b)) return emit_c_call("mod_op", a, b);
    if (a->tag != T_INT || b->tag != T_INT) return NIL;
    if (b->i == 0 || (a->i == LONG_MIN && b->i == -1)) return mk_int(0);
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
        // is_nil returns int, but we need Obj* for runtime consistency
        ds_printf(ds, "mk_int(is_nil(%s))", a->s);
        char* code_str = ds_take(ds);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
    }
    return is_nil(a) ? SYM_T : NIL;
}

// =============================================================================
// Box Operations (Mutable Reference Cells)
// =============================================================================

Value* prim_box(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    return mk_box(a);
}

Value* prim_unbox(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (!a || !is_box(a)) {
        return mk_error("unbox: expected box");
    }
    return box_get(a);
}

Value* prim_set_box(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) {
        return mk_error("set-box!: requires box and value");
    }
    if (!is_box(a)) {
        return mk_error("set-box!: first argument must be a box");
    }
    box_set(a, b);
    return b;
}

Value* prim_is_box(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    return is_box(a) ? SYM_T : NIL;
}

// =============================================================================
// I/O Operations
// =============================================================================

Value* prim_display(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (a) {
        char* str = val_to_str(a);
        if (str) {
            printf("%s", str);
            free(str);
        }
    }
    return NIL;
}

Value* prim_newline(Value* args, Value* menv) {
    (void)menv;
    (void)args;
    printf("\n");
    return NIL;
}

Value* prim_print(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (a) {
        char* str = val_to_str(a);
        if (str) {
            printf("%s\n", str);
            free(str);
        }
    }
    return NIL;
}

Value* prim_read(Value* args, Value* menv) {
    (void)menv;
    (void)args;
    char buf[256];
    if (scanf("%255s", buf) == 1) {
        // Try to parse as integer
        char* end;
        long n = strtol(buf, &end, 10);
        if (*end == '\0') {
            return mk_int(n);
        }
        // Otherwise return as symbol
        return mk_sym(buf);
    }
    return mk_error("read: EOF or error");
}

// =============================================================================
// Type Predicates
// =============================================================================

Value* prim_is_cont(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    return is_cont(a) ? SYM_T : NIL;
}

Value* prim_is_error(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    return is_error(a) ? SYM_T : NIL;
}

Value* prim_is_chan(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    return is_chan(a) ? SYM_T : NIL;
}

Value* prim_is_process(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    return is_process(a) ? SYM_T : NIL;
}

// =============================================================================
// Channel Operations (CSP)
// =============================================================================

Value* prim_make_chan(Value* args, Value* menv) {
    (void)menv;
    int capacity = 0;
    Value* a = get_one_arg(args);
    if (a && a->tag == T_INT) {
        capacity = (int)a->i;
    }
    return mk_chan(capacity);
}

// Note: Full channel implementation requires threading support
// These are placeholder implementations for the interpreter

Value* prim_chan_send(Value* args, Value* menv) {
    (void)menv;
    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) {
        return mk_error("chan-send!: requires channel and value");
    }
    if (!is_chan(a)) {
        return mk_error("chan-send!: first argument must be a channel");
    }
    // In a real implementation, this would block/schedule
    printf("chan-send!: channels require threading support\n");
    return b;
}

Value* prim_chan_recv(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (!a || !is_chan(a)) {
        return mk_error("chan-recv!: requires a channel");
    }
    // In a real implementation, this would block/schedule
    printf("chan-recv!: channels require threading support\n");
    return NIL;
}

Value* prim_chan_close(Value* args, Value* menv) {
    (void)menv;
    Value* a = get_one_arg(args);
    if (!a || !is_chan(a)) {
        return mk_error("chan-close!: requires a channel");
    }
    if (a->chan.ch) {
        a->chan.ch->closed = 1;
    }
    return NIL;
}

// =============================================================================
// Continuation Infrastructure (A3)
// =============================================================================

#include <setjmp.h>

// Continuation escape for call/cc
static ContEscape* current_cont_escape = NULL;
static int cont_tag_counter = 0;

// Prompt stack for delimited continuations
static int prompt_stack[MAX_PROMPT_DEPTH];
static int prompt_stack_top = 0;

static int next_cont_tag(void) {
    return ++cont_tag_counter;
}

static void push_prompt_tag(int tag) {
    if (prompt_stack_top < MAX_PROMPT_DEPTH) {
        prompt_stack[prompt_stack_top++] = tag;
    }
}

static void pop_prompt_tag(void) {
    if (prompt_stack_top > 0) {
        prompt_stack_top--;
    }
}

static int get_current_prompt_tag(void) {
    if (prompt_stack_top == 0) return -1;
    return prompt_stack[prompt_stack_top - 1];
}

// Continuation value that can be invoked
typedef struct {
    jmp_buf env;
    Value* result;
    int tag;
    int active;
} ContContext;

// Global for current continuation context
static ContContext* active_cont_ctx = NULL;

// eval_call_cc implements (call/cc proc)
// proc is called with a continuation k, which when invoked returns to this point
Value* eval_call_cc(Value* args, Value* menv) {
    if (is_nil(args)) {
        return mk_error("call/cc: requires a procedure");
    }

    Value* proc = eval(car(args), menv);
    if (!proc) {
        return mk_error("call/cc: procedure evaluated to nil");
    }

    int tag = next_cont_tag();
    ContContext ctx;
    ctx.tag = tag;
    ctx.active = 1;
    ctx.result = NIL;

    // Save previous context
    ContContext* prev_ctx = active_cont_ctx;
    active_cont_ctx = &ctx;

    // setjmp returns 0 on initial call, non-zero when longjmp is called
    int jumped = setjmp(ctx.env);
    if (jumped) {
        // We got here via longjmp from continuation invocation
        active_cont_ctx = prev_ctx;
        return ctx.result;
    }

    // Create continuation value that will longjmp back here
    Value* cont = mk_cont(NULL, menv, tag);

    // Apply the procedure to the continuation
    Value* arg_list = mk_cell(cont, NIL);
    Value* result;

    if (proc->tag == T_PRIM) {
        result = proc->prim(arg_list, menv);
    } else if (proc->tag == T_LAMBDA) {
        Value* params = proc->lam.params;
        Value* body = proc->lam.body;
        Value* closure_env = proc->lam.env;

        Value* new_env = closure_env;
        if (!is_nil(params)) {
            new_env = env_extend(new_env, car(params), cont);
        }

        Value* body_menv = mk_menv(menv->menv.parent, new_env);
        if (body_menv) {
            body_menv->menv.h_app = menv->menv.h_app;
            body_menv->menv.h_let = menv->menv.h_let;
            body_menv->menv.h_if = menv->menv.h_if;
            result = eval(body, body_menv);
        } else {
            result = NIL;
        }
    } else {
        result = mk_error("call/cc: not a procedure");
    }

    ctx.active = 0;
    active_cont_ctx = prev_ctx;
    return result;
}

// Invoke a continuation with a value
Value* invoke_continuation(Value* cont, Value* val) {
    if (!cont || cont->tag != T_CONT) {
        return mk_error("not a continuation");
    }

    int tag = cont->cont.tag;

    // Find the matching continuation context
    ContContext* ctx = active_cont_ctx;
    while (ctx && ctx->tag != tag) {
        // In a more complex implementation, we'd search a stack
        // For now, just check the current active context
        break;
    }

    if (ctx && ctx->active && ctx->tag == tag) {
        ctx->result = val;
        longjmp(ctx->env, 1);
    }

    return mk_error("continuation no longer valid");
}

// =============================================================================
// Delimited Continuations (prompt/control) - A3
// =============================================================================

// Prompt escape structure for control operator
typedef struct {
    jmp_buf env;
    Value* result;
    int tag;
    int active;
    Value* captured_k;  // The captured continuation
} PromptContext;

static PromptContext* prompt_contexts[MAX_PROMPT_DEPTH];
static int prompt_context_top = 0;

// eval_prompt implements (prompt body)
// Establishes a delimiter for control operator
Value* eval_prompt(Value* args, Value* menv) {
    if (is_nil(args)) {
        return NIL;
    }

    Value* body = car(args);
    int tag = next_cont_tag();

    PromptContext ctx;
    ctx.tag = tag;
    ctx.active = 1;
    ctx.result = NIL;
    ctx.captured_k = NIL;

    // Push context
    if (prompt_context_top < MAX_PROMPT_DEPTH) {
        prompt_contexts[prompt_context_top++] = &ctx;
    }
    push_prompt_tag(tag);

    int jumped = setjmp(ctx.env);
    if (jumped) {
        // Got here via control - ctx.result contains the result
        pop_prompt_tag();
        prompt_context_top--;
        return ctx.result;
    }

    Value* result = eval(body, menv);

    pop_prompt_tag();
    prompt_context_top--;
    ctx.active = 0;

    return result;
}

// eval_control implements (control k body)
// Captures the continuation up to the enclosing prompt and binds it to k
Value* eval_control(Value* args, Value* menv) {
    if (is_nil(args) || is_nil(cdr(args))) {
        return mk_error("control: requires variable and body");
    }

    Value* k_sym = car(args);
    if (!k_sym || k_sym->tag != T_SYM) {
        return mk_error("control: first argument must be a symbol");
    }

    Value* body = car(cdr(args));

    int prompt_tag = get_current_prompt_tag();
    if (prompt_tag < 0) {
        return mk_error("control: no enclosing prompt");
    }

    // Find the matching prompt context
    PromptContext* ctx = NULL;
    for (int i = prompt_context_top - 1; i >= 0; i--) {
        if (prompt_contexts[i]->tag == prompt_tag) {
            ctx = prompt_contexts[i];
            break;
        }
    }

    if (!ctx || !ctx->active) {
        return mk_error("control: prompt context not found");
    }

    // Create a continuation that, when invoked, returns to the prompt
    // For simplicity, this continuation just returns its argument
    // A full implementation would reinstall the prompt context
    Value* cont = mk_cont(NULL, menv, prompt_tag);

    // Evaluate body with k bound to the continuation
    Value* new_env = env_extend(menv->menv.env, k_sym, cont);
    Value* body_menv = mk_menv(menv->menv.parent, new_env);
    if (!body_menv) {
        return mk_error("control: failed to create menv");
    }
    body_menv->menv.h_app = menv->menv.h_app;
    body_menv->menv.h_let = menv->menv.h_let;
    body_menv->menv.h_if = menv->menv.h_if;

    Value* result = eval(body, body_menv);

    // Jump back to the prompt with the result
    ctx->result = result;
    longjmp(ctx->env, 1);

    // Never reached
    return NIL;
}

// =============================================================================
// Cooperative Scheduler (A4)
// =============================================================================

#define MAX_PROCESSES 256

typedef struct {
    Value* processes[MAX_PROCESSES];  // Run queue
    int head;
    int tail;
    int count;
    Value* current;   // Currently running process
    int running;      // Is scheduler running?
} Scheduler;

static Scheduler global_scheduler = {.head = 0, .tail = 0, .count = 0, .current = NULL, .running = 0};

// Add process to run queue
static void scheduler_enqueue(Value* proc) {
    if (global_scheduler.count >= MAX_PROCESSES) {
        return;
    }
    global_scheduler.processes[global_scheduler.tail] = proc;
    global_scheduler.tail = (global_scheduler.tail + 1) % MAX_PROCESSES;
    global_scheduler.count++;
}

// Remove process from run queue
static Value* scheduler_dequeue(void) {
    if (global_scheduler.count == 0) {
        return NULL;
    }
    Value* proc = global_scheduler.processes[global_scheduler.head];
    global_scheduler.head = (global_scheduler.head + 1) % MAX_PROCESSES;
    global_scheduler.count--;
    return proc;
}

// Spawn a new process (green thread)
Value* scheduler_spawn(Value* thunk, Value* menv) {
    Value* proc = mk_process(thunk);
    if (!proc) return NIL;

    proc->proc.state = PROC_READY;
    proc->proc.menv = menv;

    scheduler_enqueue(proc);
    return proc;
}

// Run a single process step
static void run_process(Value* proc, Value* menv) {
    if (!proc || proc->proc.state != PROC_READY) {
        return;
    }

    proc->proc.state = PROC_RUNNING;
    global_scheduler.current = proc;

    Value* thunk = proc->proc.thunk;
    if (thunk && thunk->tag == T_LAMBDA) {
        // Create a new menv for this process
        Value* proc_menv = mk_menv(menv->menv.parent, thunk->lam.env);
        if (proc_menv) {
            proc_menv->menv.h_app = menv->menv.h_app;
            proc_menv->menv.h_let = menv->menv.h_let;
            proc_menv->menv.h_if = menv->menv.h_if;

            // Evaluate the thunk body (no args)
            proc->proc.result = eval(thunk->lam.body, proc_menv);
        }
    }

    proc->proc.state = PROC_DONE;
    global_scheduler.current = NULL;
}

// Run scheduler until all processes complete
void scheduler_run(Value* menv) {
    if (global_scheduler.running) {
        return;
    }
    global_scheduler.running = 1;

    while (global_scheduler.count > 0) {
        Value* proc = scheduler_dequeue();
        if (proc) {
            run_process(proc, menv);
        }
    }

    global_scheduler.running = 0;
}

// Park current process (for blocking channel operations)
void scheduler_park(Value* proc) {
    if (proc) {
        proc->proc.state = PROC_PARKED;
    }
}

// Unpark a process with a value
void scheduler_unpark(Value* proc, Value* val) {
    if (proc && proc->proc.state == PROC_PARKED) {
        proc->proc.state = PROC_READY;
        proc->proc.park_value = val;
        scheduler_enqueue(proc);
    }
}

// eval_go implements (go expr)
// Spawns a green thread to evaluate expr
Value* eval_go(Value* args, Value* menv) {
    if (is_nil(args)) {
        return mk_error("go: requires an expression");
    }

    Value* expr = car(args);

    // Create a thunk (lambda with no args) that evaluates the expression
    Value* thunk = mk_lambda(NIL, expr, menv->menv.env);

    // Spawn the process
    Value* proc = scheduler_spawn(thunk, menv);

    // Start the scheduler if not already running
    if (!global_scheduler.running && global_scheduler.count > 0) {
        scheduler_run(menv);
    }

    return proc;
}

// =============================================================================
// Channel Operations with Continuation Parking (A4)
// =============================================================================

// Channel send with proper blocking
static Value* chan_send_blocking(Value* ch, Value* val, Value* menv) {
    if (!ch || ch->tag != T_CHAN || !ch->chan.ch) {
        return mk_error("chan-send!: invalid channel");
    }

    Channel* chan = ch->chan.ch;

    if (chan->closed) {
        return mk_error("chan-send!: channel closed");
    }

    // Check if there's a waiting receiver
    if (!is_nil(chan->recv_waiters) && chan->recv_waiters->tag == T_CELL) {
        Value* waiter = car(chan->recv_waiters);
        chan->recv_waiters = cdr(chan->recv_waiters);

        // Unpark the receiver with the value
        if (waiter && waiter->tag == T_PROCESS) {
            scheduler_unpark(waiter, val);
        }
        return val;
    }

    // Buffered channel: try to add to buffer
    if (chan->capacity > 0 && chan->count < chan->capacity) {
        chan->buffer[chan->tail] = val;
        chan->tail = (chan->tail + 1) % chan->capacity;
        chan->count++;
        return val;
    }

    // Unbuffered or buffer full: need to wait
    // Add current process to send waiters
    Value* current = global_scheduler.current;
    if (current) {
        // Create (process . value) pair
        Value* pair = mk_cell(current, val);
        chan->send_waiters = mk_cell(pair, chan->send_waiters);
        scheduler_park(current);
        // In a real cooperative scheduler, we'd yield here
    }

    return val;
}

// Channel receive with proper blocking
static Value* chan_recv_blocking(Value* ch, Value* menv) {
    if (!ch || ch->tag != T_CHAN || !ch->chan.ch) {
        return mk_error("chan-recv!: invalid channel");
    }

    Channel* chan = ch->chan.ch;

    // Check buffer first
    if (chan->count > 0) {
        Value* val = chan->buffer[chan->head];
        chan->head = (chan->head + 1) % chan->capacity;
        chan->count--;

        // Check if there's a waiting sender
        if (!is_nil(chan->send_waiters) && chan->send_waiters->tag == T_CELL) {
            Value* waiter_pair = car(chan->send_waiters);
            chan->send_waiters = cdr(chan->send_waiters);

            if (waiter_pair && waiter_pair->tag == T_CELL) {
                Value* waiter = car(waiter_pair);
                Value* waiter_val = cdr(waiter_pair);

                // Add sender's value to buffer
                chan->buffer[chan->tail] = waiter_val;
                chan->tail = (chan->tail + 1) % chan->capacity;
                chan->count++;

                // Unpark the sender
                if (waiter && waiter->tag == T_PROCESS) {
                    scheduler_unpark(waiter, waiter_val);
                }
            }
        }

        return val;
    }

    // Check if there's a waiting sender (unbuffered case)
    if (!is_nil(chan->send_waiters) && chan->send_waiters->tag == T_CELL) {
        Value* waiter_pair = car(chan->send_waiters);
        chan->send_waiters = cdr(chan->send_waiters);

        if (waiter_pair && waiter_pair->tag == T_CELL) {
            Value* waiter = car(waiter_pair);
            Value* val = cdr(waiter_pair);

            // Unpark the sender
            if (waiter && waiter->tag == T_PROCESS) {
                scheduler_unpark(waiter, val);
            }

            return val;
        }
    }

    // Channel closed and empty
    if (chan->closed) {
        return NIL;
    }

    // Need to wait: add current process to recv waiters
    Value* current = global_scheduler.current;
    if (current) {
        chan->recv_waiters = mk_cell(current, chan->recv_waiters);
        scheduler_park(current);
        // In a real cooperative scheduler, we'd yield here and return park_value
        return current->proc.park_value;
    }

    return NIL;
}

// eval_select implements (select clauses...)
// Each clause is ((recv ch) => body) or ((send ch val) => body) or (default => body)
Value* eval_select(Value* args, Value* menv) {
    if (is_nil(args)) {
        return NIL;
    }

    Value* default_body = NULL;

    // First pass: check for ready channels
    Value* clauses = args;
    while (!is_nil(clauses)) {
        Value* clause = car(clauses);
        if (!clause || clause->tag != T_CELL) {
            clauses = cdr(clauses);
            continue;
        }

        Value* op = car(clause);

        // Check for default clause
        if (op && op->tag == T_SYM && sym_eq_str(op, "default")) {
            default_body = car(cdr(clause));
            clauses = cdr(clauses);
            continue;
        }

        // Check for (recv ch) or (send ch val)
        if (op && op->tag == T_CELL) {
            Value* op_type = car(op);

            if (op_type && op_type->tag == T_SYM) {
                if (sym_eq_str(op_type, "recv")) {
                    // (recv ch)
                    Value* ch_expr = car(cdr(op));
                    Value* ch = eval(ch_expr, menv);

                    if (ch && ch->tag == T_CHAN && ch->chan.ch) {
                        Channel* chan = ch->chan.ch;

                        // Check if channel has data or waiting sender
                        if (chan->count > 0 || !is_nil(chan->send_waiters)) {
                            // Find body after =>
                            Value* rest = cdr(clause);
                            while (!is_nil(rest)) {
                                Value* item = car(rest);
                                if (item && item->tag == T_SYM && sym_eq_str(item, "=>")) {
                                    Value* body = car(cdr(rest));
                                    // Do the receive
                                    Value* val = chan_recv_blocking(ch, menv);
                                    (void)val;  // Could bind to variable
                                    return eval(body, menv);
                                }
                                rest = cdr(rest);
                            }
                        }
                    }
                } else if (sym_eq_str(op_type, "send")) {
                    // (send ch val)
                    Value* ch_expr = car(cdr(op));
                    Value* val_expr = car(cdr(cdr(op)));
                    Value* ch = eval(ch_expr, menv);

                    if (ch && ch->tag == T_CHAN && ch->chan.ch) {
                        Channel* chan = ch->chan.ch;

                        // Check if channel can accept or has waiting receiver
                        int can_send = !is_nil(chan->recv_waiters);
                        if (chan->capacity > 0) {
                            can_send = can_send || (chan->count < chan->capacity);
                        }

                        if (can_send) {
                            // Find body after =>
                            Value* rest = cdr(clause);
                            while (!is_nil(rest)) {
                                Value* item = car(rest);
                                if (item && item->tag == T_SYM && sym_eq_str(item, "=>")) {
                                    Value* body = car(cdr(rest));
                                    Value* val = eval(val_expr, menv);
                                    chan_send_blocking(ch, val, menv);
                                    return eval(body, menv);
                                }
                                rest = cdr(rest);
                            }
                        }
                    }
                }
            }
        }

        clauses = cdr(clauses);
    }

    // No ready channel - use default if available
    if (default_body) {
        return eval(default_body, menv);
    }

    // No default and nothing ready - would need to block
    // For now, just return nil
    return NIL;
}

// =============================================================================
// deftype Implementation (A5)
// =============================================================================

// Simple user type registry (separate from codegen's TypeDef)
#define MAX_USER_TYPES 64
#define MAX_USER_FIELDS 16

typedef struct {
    char* name;
    char* field_names[MAX_USER_FIELDS];
    char* field_types[MAX_USER_FIELDS];
    int field_count;
    int is_weak[MAX_USER_FIELDS];  // Track weak fields for back-edge analysis
} UserTypeDef;

static UserTypeDef user_type_registry[MAX_USER_TYPES];
static int user_type_count = 0;

// Register a new user type
static int user_register_type(const char* name) {
    if (user_type_count >= MAX_USER_TYPES) return -1;

    int idx = user_type_count++;
    user_type_registry[idx].name = strdup(name);
    user_type_registry[idx].field_count = 0;
    return idx;
}

// Add a field to a user type
static void user_add_type_field(int type_idx, const char* name, const char* type, int is_weak) {
    if (type_idx < 0 || type_idx >= user_type_count) return;
    UserTypeDef* td = &user_type_registry[type_idx];
    if (td->field_count >= MAX_USER_FIELDS) return;

    int idx = td->field_count++;
    td->field_names[idx] = strdup(name);
    td->field_types[idx] = strdup(type);
    td->is_weak[idx] = is_weak;
}

// Find a user type by name
static UserTypeDef* user_find_type(const char* name) {
    for (int i = 0; i < user_type_count; i++) {
        if (strcmp(user_type_registry[i].name, name) == 0) {
            return &user_type_registry[i];
        }
    }
    return NULL;
}

// User type instance structure stored in cell
// We'll represent user types as (type-name . ((field1 . val1) (field2 . val2) ...))

// Check if value is a user type instance
static int is_user_type(Value* v, const char* type_name) {
    if (!v || v->tag != T_CELL) return 0;
    Value* tag = car(v);
    if (!tag || tag->tag != T_SYM) return 0;

    // Check for #:type-name format
    char expected[128];
    snprintf(expected, sizeof(expected), "#:%s", type_name);
    return strcmp(tag->s, expected) == 0;
}

// Get field from user type instance
static Value* user_type_get_field(Value* v, const char* field_name) {
    if (!v || v->tag != T_CELL) return NIL;

    Value* fields = cdr(v);
    while (!is_nil(fields) && fields->tag == T_CELL) {
        Value* pair = car(fields);
        if (pair && pair->tag == T_CELL) {
            Value* name = car(pair);
            if (name && name->tag == T_SYM && strcmp(name->s, field_name) == 0) {
                return cdr(pair);
            }
        }
        fields = cdr(fields);
    }
    return NIL;
}

// Set field in user type instance
static void user_type_set_field(Value* v, const char* field_name, Value* val) {
    if (!v || v->tag != T_CELL) return;

    Value* fields = cdr(v);
    while (!is_nil(fields) && fields->tag == T_CELL) {
        Value* pair = car(fields);
        if (pair && pair->tag == T_CELL) {
            Value* name = car(pair);
            if (name && name->tag == T_SYM && strcmp(name->s, field_name) == 0) {
                pair->cell.cdr = val;
                return;
            }
        }
        fields = cdr(fields);
    }
}

// Primitive for constructor: (mk-TypeName field1 field2 ...)
typedef struct {
    char type_name[64];
    char* field_names[MAX_USER_FIELDS];
    int field_count;
} ConstructorData;

static ConstructorData constructors[MAX_USER_TYPES];
static int constructor_count = 0;

// Create primitives for a type
static void create_type_primitives(const char* type_name, int field_count, char** field_names, Value* menv) {
    char buf[128];

    // Store constructor data
    if (constructor_count >= MAX_USER_TYPES) return;
    int cidx = constructor_count++;
    strncpy(constructors[cidx].type_name, type_name, 63);
    constructors[cidx].type_name[63] = '\0';
    constructors[cidx].field_count = field_count;
    for (int i = 0; i < field_count; i++) {
        constructors[cidx].field_names[i] = strdup(field_names[i]);
    }

    // Create constructor: mk-TypeName
    snprintf(buf, sizeof(buf), "mk-%s", type_name);

    // We need to create a closure that captures the type info
    // Since C doesn't have closures, we'll create a lambda
    // The constructor will be: (lambda args (make-instance type-name args))

    // For simplicity, we'll define constructors that work for up to 4 fields
    // and use the global constructors array to look up field info

    // Register as primitive with unique ID
    // This is a simplified implementation - in production you'd use proper closure support
    Value* constructor_lam = mk_lambda(
        mk_sym("args"),  // Rest args
        mk_cell(mk_sym("make-type-instance"),
            mk_cell(mk_sym(type_name),
                mk_cell(mk_sym("args"), NIL))),
        menv->menv.env
    );
    global_define(mk_sym(buf), constructor_lam);

    // Create accessors: TypeName-fieldName
    for (int i = 0; i < field_count; i++) {
        snprintf(buf, sizeof(buf), "%s-%s", type_name, field_names[i]);

        // Create accessor lambda
        Value* accessor_lam = mk_lambda(
            mk_cell(mk_sym("obj"), NIL),
            mk_cell(mk_sym("type-get-field"),
                mk_cell(mk_sym("obj"),
                    mk_cell(mk_sym(field_names[i]), NIL))),
            menv->menv.env
        );
        global_define(mk_sym(buf), accessor_lam);

        // Create setter: set-TypeName-fieldName!
        snprintf(buf, sizeof(buf), "set-%s-%s!", type_name, field_names[i]);
        Value* setter_lam = mk_lambda(
            mk_cell(mk_sym("obj"), mk_cell(mk_sym("val"), NIL)),
            mk_cell(mk_sym("type-set-field!"),
                mk_cell(mk_sym("obj"),
                    mk_cell(mk_sym(field_names[i]),
                        mk_cell(mk_sym("val"), NIL)))),
            menv->menv.env
        );
        global_define(mk_sym(buf), setter_lam);
    }

    // Create predicate: TypeName?
    snprintf(buf, sizeof(buf), "%s?", type_name);
    Value* pred_lam = mk_lambda(
        mk_cell(mk_sym("obj"), NIL),
        mk_cell(mk_sym("type-is?"),
            mk_cell(mk_sym("obj"),
                mk_cell(mk_sym(type_name), NIL))),
        menv->menv.env
    );
    global_define(mk_sym(buf), pred_lam);
}

// Primitive to create a type instance
Value* prim_make_type_instance(Value* args, Value* menv) {
    (void)menv;

    if (is_nil(args)) return mk_error("make-type-instance: requires type name");

    Value* type_name_val = car(args);
    if (!type_name_val || type_name_val->tag != T_SYM) {
        return mk_error("make-type-instance: type name must be a symbol");
    }

    const char* type_name = type_name_val->s;
    UserTypeDef* td = user_find_type(type_name);
    if (!td) {
        return mk_error("make-type-instance: unknown type");
    }

    // Build field list from args
    Value* field_vals = car(cdr(args));  // args list
    Value* fields = NIL;

    // Build fields in reverse order, then reverse
    int i = 0;
    Value* arg = field_vals;
    while (i < td->field_count && !is_nil(arg)) {
        Value* val = (arg->tag == T_CELL) ? car(arg) : arg;
        Value* pair = mk_cell(mk_sym(td->field_names[i]), val);
        fields = mk_cell(pair, fields);
        i++;
        if (arg->tag == T_CELL) {
            arg = cdr(arg);
        } else {
            break;
        }
    }

    // Reverse fields to get correct order
    Value* rev_fields = NIL;
    while (!is_nil(fields)) {
        rev_fields = mk_cell(car(fields), rev_fields);
        fields = cdr(fields);
    }

    // Create type tag
    char tag_buf[128];
    snprintf(tag_buf, sizeof(tag_buf), "#:%s", type_name);

    return mk_cell(mk_sym(tag_buf), rev_fields);
}

// Primitive to get field from type instance
Value* prim_type_get_field(Value* args, Value* menv) {
    (void)menv;

    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) {
        return mk_error("type-get-field: requires object and field name");
    }

    if (!b || b->tag != T_SYM) {
        return mk_error("type-get-field: field name must be a symbol");
    }

    return user_type_get_field(a, b->s);
}

// Primitive to set field in type instance
Value* prim_type_set_field(Value* args, Value* menv) {
    (void)menv;

    if (is_nil(args) || is_nil(cdr(args)) || is_nil(cdr(cdr(args)))) {
        return mk_error("type-set-field!: requires object, field name, and value");
    }

    Value* obj = car(args);
    Value* field = car(cdr(args));
    Value* val = car(cdr(cdr(args)));

    if (!field || field->tag != T_SYM) {
        return mk_error("type-set-field!: field name must be a symbol");
    }

    user_type_set_field(obj, field->s, val);
    return val;
}

// Primitive to check if object is of given type
Value* prim_type_is(Value* args, Value* menv) {
    (void)menv;

    Value* a; Value* b;
    if (!get_two_args(args, &a, &b)) {
        return NIL;
    }

    if (!b || b->tag != T_SYM) {
        return NIL;
    }

    return is_user_type(a, b->s) ? SYM_T : NIL;
}

// eval_deftype implements (deftype TypeName (field1 Type1) (field2 Type2 :weak) ...)
Value* eval_deftype(Value* args, Value* menv) {
    if (is_nil(args)) {
        return mk_error("deftype: requires type name");
    }

    Value* type_name_val = car(args);
    if (!type_name_val || type_name_val->tag != T_SYM) {
        return mk_error("deftype: type name must be a symbol");
    }

    const char* type_name = type_name_val->s;

    // Check if type already exists
    if (user_find_type(type_name)) {
        return mk_error("deftype: type already defined");
    }

    // Register the type
    int type_idx = user_register_type(type_name);
    if (type_idx < 0) {
        return mk_error("deftype: too many types");
    }

    // Parse field definitions
    char* field_names[MAX_USER_FIELDS];
    int field_count = 0;

    Value* field_defs = cdr(args);
    while (!is_nil(field_defs) && field_defs->tag == T_CELL) {
        Value* field_def = car(field_defs);

        if (!field_def || field_def->tag != T_CELL) {
            field_defs = cdr(field_defs);
            continue;
        }

        Value* field_name = car(field_def);
        Value* field_type = car(cdr(field_def));

        if (!field_name || field_name->tag != T_SYM) {
            field_defs = cdr(field_defs);
            continue;
        }

        const char* fname = field_name->s;
        const char* ftype = (field_type && field_type->tag == T_SYM) ? field_type->s : "any";

        // Check for :weak annotation
        int is_weak = 0;
        Value* annotation = car(cdr(cdr(field_def)));
        if (annotation && annotation->tag == T_SYM && strcmp(annotation->s, ":weak") == 0) {
            is_weak = 1;
        }

        user_add_type_field(type_idx, fname, ftype, is_weak);

        if (field_count < MAX_USER_FIELDS) {
            field_names[field_count++] = strdup(fname);
        }

        field_defs = cdr(field_defs);
    }

    // Create primitives for the type
    create_type_primitives(type_name, field_count, field_names, menv);

    // Clean up
    for (int i = 0; i < field_count; i++) {
        free(field_names[i]);
    }

    return type_name_val;
}

// =============================================================================
// Register deftype primitives in environment
// =============================================================================

void register_deftype_primitives(Value* env) {
    // Register helper primitives for deftype
    global_define(mk_sym("make-type-instance"), mk_prim(prim_make_type_instance));
    global_define(mk_sym("type-get-field"), mk_prim(prim_type_get_field));
    global_define(mk_sym("type-set-field!"), mk_prim(prim_type_set_field));
    global_define(mk_sym("type-is?"), mk_prim(prim_type_is));
}
