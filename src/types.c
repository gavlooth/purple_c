#define _POSIX_C_SOURCE 200809L
#include "types.h"
#include <string.h>

// -- Value Constructors --

Value* alloc_val(Tag tag) {
    Value* v = malloc(sizeof(Value));
    v->tag = tag;
    return v;
}

Value* mk_int(long i) {
    Value* v = alloc_val(T_INT);
    v->i = i;
    return v;
}

Value* mk_sym(const char* s) {
    Value* v = alloc_val(T_SYM);
    v->s = strdup(s);
    return v;
}

Value* mk_cell(Value* car, Value* cdr) {
    Value* v = alloc_val(T_CELL);
    v->cell.car = car;
    v->cell.cdr = cdr;
    return v;
}

Value* mk_prim(PrimFn fn) {
    Value* v = alloc_val(T_PRIM);
    v->prim = fn;
    return v;
}

Value* mk_code(const char* s) {
    Value* v = alloc_val(T_CODE);
    v->s = strdup(s);
    return v;
}

Value* mk_lambda(Value* params, Value* body, Value* env) {
    Value* v = alloc_val(T_LAMBDA);
    v->lam.params = params;
    v->lam.body = body;
    v->lam.env = env;
    return v;
}

// -- Value Helpers --

int is_nil(Value* v) {
    return v == NULL || v->tag == T_NIL;
}

int is_code(Value* v) {
    return v && v->tag == T_CODE;
}

Value* car(Value* v) {
    return (v && v->tag == T_CELL) ? v->cell.car : NULL;
}

Value* cdr(Value* v) {
    return (v && v->tag == T_CELL) ? v->cell.cdr : NULL;
}

int sym_eq(Value* s1, Value* s2) {
    if (!s1 || !s2) return 0;
    if (s1->tag != T_SYM || s2->tag != T_SYM) return 0;
    return strcmp(s1->s, s2->s) == 0;
}

int sym_eq_str(Value* s1, const char* s2) {
    if (!s1 || s1->tag != T_SYM) return 0;
    return strcmp(s1->s, s2) == 0;
}

char* list_to_str(Value* v) {
    char buf[4096] = "(";
    while (v && !is_nil(v)) {
        char* s = val_to_str(car(v));
        strcat(buf, s);
        free(s);
        v = cdr(v);
        if (v && !is_nil(v)) strcat(buf, " ");
    }
    strcat(buf, ")");
    return strdup(buf);
}

char* val_to_str(Value* v) {
    if (!v) return strdup("NULL");
    char buf[4096];
    switch (v->tag) {
        case T_INT:
            sprintf(buf, "%ld", v->i);
            return strdup(buf);
        case T_SYM:
            return strdup(v->s);
        case T_CODE:
            return strdup(v->s);
        case T_CELL:
            return list_to_str(v);
        case T_NIL:
            return strdup("()");
        case T_PRIM:
            return strdup("#<prim>");
        case T_LAMBDA:
            return strdup("#<lambda>");
        case T_MENV:
            return strdup("#<menv>");
        default:
            return strdup("?");
    }
}
