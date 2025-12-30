#ifndef PURPLE_TYPES_H
#define PURPLE_TYPES_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// -- Core Value Types --

typedef enum {
    T_INT, T_SYM, T_CELL, T_NIL, T_PRIM, T_MENV, T_CODE, T_LAMBDA
} Tag;

struct Value;

// Function pointer types
typedef struct Value* (*PrimFn)(struct Value* args, struct Value* menv);
typedef struct Value* (*HandlerFn)(struct Value* exp, struct Value* menv);

// Core Value structure
typedef struct Value {
    Tag tag;
    union {
        long i;                          // T_INT
        char* s;                         // T_SYM, T_CODE
        struct { struct Value* car; struct Value* cdr; } cell;  // T_CELL
        PrimFn prim;                     // T_PRIM
        struct {                         // T_MENV
            struct Value* env;
            struct Value* parent;
            HandlerFn h_app;
            HandlerFn h_let;
            HandlerFn h_if;
            HandlerFn h_lit;
            HandlerFn h_var;
        } menv;
        struct {                         // T_LAMBDA
            struct Value* params;
            struct Value* body;
            struct Value* env;
        } lam;
    };
} Value;

// -- Value Constructors --
Value* alloc_val(Tag tag);
Value* mk_int(long i);
Value* mk_sym(const char* s);
Value* mk_cell(Value* car, Value* cdr);
Value* mk_prim(PrimFn fn);
Value* mk_code(const char* s);
Value* mk_lambda(Value* params, Value* body, Value* env);

// -- Value Helpers --
int is_nil(Value* v);
int is_code(Value* v);
Value* car(Value* v);
Value* cdr(Value* v);
char* val_to_str(Value* v);
char* list_to_str(Value* v);

// Symbol comparison
int sym_eq(Value* s1, Value* s2);
int sym_eq_str(Value* s1, const char* s2);

// -- List Construction --
#define LIST1(a) mk_cell(a, NIL)
#define LIST2(a,b) mk_cell(a, mk_cell(b, NIL))
#define LIST3(a,b,c) mk_cell(a, mk_cell(b, mk_cell(c, NIL)))

#endif // PURPLE_TYPES_H
