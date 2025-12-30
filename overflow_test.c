#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Mock types from types.h
typedef enum { T_INT, T_SYM, T_CELL, T_NIL } Tag;
typedef struct Value {
    Tag tag;
    union {
        long i;
        char* s;
        struct { struct Value* car; struct Value* cdr; } cell;
    };
} Value;

Value* NIL = NULL;
Value* mk_cell(Value* car, Value* cdr) {
    Value* v = malloc(sizeof(Value));
    v->tag = T_CELL; v->cell.car = car; v->cell.cdr = cdr;
    return v;
}
Value* mk_int(long i) {
    Value* v = malloc(sizeof(Value));
    v->tag = T_INT; v->i = i;
    return v;
}

// Copy of list_to_str from types.c
char* val_to_str(Value* v);
Value* car(Value* v) { return v->cell.car; }
Value* cdr(Value* v) { return v->cell.cdr; }
int is_nil(Value* v) { return v == NULL || v->tag == T_NIL; }

char* list_to_str(Value* v) {
    char buf[4096] = "("; // FIXED BUFFER
    while (v && !is_nil(v)) {
        char* s = val_to_str(car(v));
        strcat(buf, s); // DANGEROUS
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
        case T_INT: sprintf(buf, "%ld", v->i); return strdup(buf);
        case T_CELL: return list_to_str(v);
        case T_NIL: return strdup("()");
        default: return strdup("?");
    }
}

int main() {
    // Create a list long enough to overflow 4096 bytes
    Value* list = NIL;
    for (int i = 0; i < 2000; i++) {
        list = mk_cell(mk_int(1234567890), list);
    }
    printf("Created list. Converting to str...\n");
    char* s = val_to_str(list);
    printf("Result length: %lu\n", strlen(s));
    free(s);
    return 0;
}
