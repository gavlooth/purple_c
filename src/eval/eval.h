#ifndef PURPLE_EVAL_H
#define PURPLE_EVAL_H

#include "../types.h"

// -- Meta-Environment (MEnv) --
// Contains environment bindings and semantic handlers

// Forward declarations
struct Value;

// Handler function type
typedef struct Value* (*HandlerFn)(struct Value* exp, struct Value* menv);

// MEnv accessors
Value* menv_env(Value* menv);
Value* menv_parent(Value* menv);

// MEnv construction
Value* mk_menv(Value* parent, Value* env);

// Environment operations
Value* env_lookup(Value* env, Value* sym);
Value* env_extend(Value* env, Value* sym, Value* val);

// -- Evaluation --

Value* eval(Value* expr, Value* menv);
Value* eval_list(Value* list, Value* menv);

// -- Default Handlers --

Value* h_lit_default(Value* exp, Value* menv);
Value* h_var_default(Value* exp, Value* menv);
Value* h_app_default(Value* exp, Value* menv);
Value* h_let_default(Value* exp, Value* menv);
Value* h_if_default(Value* exp, Value* menv);

// -- Primitives --

// Arithmetic
Value* prim_add(Value* args, Value* menv);
Value* prim_sub(Value* args, Value* menv);
Value* prim_mul(Value* args, Value* menv);
Value* prim_div(Value* args, Value* menv);
Value* prim_mod(Value* args, Value* menv);

// Comparison
Value* prim_eq(Value* args, Value* menv);
Value* prim_lt(Value* args, Value* menv);
Value* prim_gt(Value* args, Value* menv);
Value* prim_le(Value* args, Value* menv);
Value* prim_ge(Value* args, Value* menv);

// Logical
Value* prim_not(Value* args, Value* menv);

// List operations
Value* prim_cons(Value* args, Value* menv);
Value* prim_car(Value* args, Value* menv);
Value* prim_cdr(Value* args, Value* menv);
Value* prim_fst(Value* args, Value* menv);
Value* prim_snd(Value* args, Value* menv);
Value* prim_null(Value* args, Value* menv);

// Other
Value* prim_run(Value* args, Value* menv);

// -- Symbol Table --

extern Value* NIL;
extern Value* SYM_T;
extern Value* SYM_QUOTE;
extern Value* SYM_IF;
extern Value* SYM_LAMBDA;
extern Value* SYM_LET;
extern Value* SYM_LETREC;
extern Value* SYM_AND;
extern Value* SYM_OR;
extern Value* SYM_LIFT;
extern Value* SYM_RUN;
extern Value* SYM_EM;
extern Value* SYM_SCAN;
extern Value* SYM_GET_META;
extern Value* SYM_SET_META;

void init_syms(void);

#endif // PURPLE_EVAL_H
