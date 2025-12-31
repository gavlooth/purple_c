#define _POSIX_C_SOURCE 200809L
#include "types.h"
#include "util/dstring.h"
#include <string.h>

// -- Compiler Arena (Phase 12) --
// Global arena for all compiler-phase allocations

typedef struct ArenaBlock {
    char* memory;
    size_t size;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

typedef struct StringNode {
    char* s;
    struct StringNode* next;
} StringNode;

static ArenaBlock* compiler_arena_blocks = NULL;
static ArenaBlock* compiler_arena_current = NULL;
static StringNode* compiler_strings = NULL;
static size_t compiler_arena_block_size = 65536;  // 64KB blocks
static int compiler_arena_string_oom = 0;

static void* compiler_arena_alloc(size_t size);

void compiler_arena_init(void) {
    compiler_arena_blocks = NULL;
    compiler_arena_current = NULL;
    compiler_strings = NULL;
    compiler_arena_string_oom = 0;
    // Allocate initial block so compiler allocations use the arena
    (void)compiler_arena_alloc(0);
}

static void* compiler_arena_alloc(size_t size) {
    // Align to 8 bytes
    size = (size + 7) & ~(size_t)7;

    if (!compiler_arena_current || compiler_arena_current->used + size > compiler_arena_current->size) {
        // Need new block
        size_t bs = compiler_arena_block_size;
        if (size > bs) bs = size;

        ArenaBlock* b = malloc(sizeof(ArenaBlock));
        if (!b) return NULL;
        b->memory = malloc(bs);
        if (!b->memory) {
            free(b);
            return NULL;
        }
        b->size = bs;
        b->used = 0;
        b->next = compiler_arena_blocks;
        compiler_arena_blocks = b;
        compiler_arena_current = b;
    }

    void* ptr = compiler_arena_current->memory + compiler_arena_current->used;
    compiler_arena_current->used += size;
    return ptr;
}

void compiler_arena_register_string(char* s) {
    if (!s || compiler_arena_string_oom) return;
    StringNode* node = compiler_arena_alloc(sizeof(StringNode));
    if (!node) {
        compiler_arena_string_oom = 1;
        fprintf(stderr, "Warning: OOM tracking compiler string; potential leak\n");
        return;
    }
    node->s = s;
    node->next = compiler_strings;
    compiler_strings = node;
}

void compiler_arena_cleanup(void) {
    // Free all strings
    StringNode* sn = compiler_strings;
    while (sn) {
        StringNode* next = sn->next;
        free(sn->s);
        sn = next;
    }
    compiler_strings = NULL;

    // Free all arena blocks
    ArenaBlock* b = compiler_arena_blocks;
    while (b) {
        ArenaBlock* next = b->next;
        free(b->memory);
        free(b);
        b = next;
    }
    compiler_arena_blocks = NULL;
    compiler_arena_current = NULL;
}

// -- Value Constructors --

Value* alloc_val(Tag tag) {
    Value* v;
    if (compiler_arena_current) {
        v = compiler_arena_alloc(sizeof(Value));
    } else {
        v = malloc(sizeof(Value));
    }
    if (!v) return NULL;
    v->tag = tag;
    return v;
}

Value* mk_int(long i) {
    Value* v = alloc_val(T_INT);
    if (!v) return NULL;
    v->i = i;
    return v;
}

Value* mk_sym(const char* s) {
    if (!s) s = "";
    Value* v = alloc_val(T_SYM);
    if (!v) return NULL;
    v->s = s ? strdup(s) : NULL;
    if (s && !v->s) {
        // Don't free v if using arena (arena will bulk free)
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (v->s && compiler_arena_current) {
        compiler_arena_register_string(v->s);
    }
    return v;
}

Value* mk_cell(Value* car, Value* cdr) {
    Value* v = alloc_val(T_CELL);
    if (!v) return NULL;
    v->cell.car = car;
    v->cell.cdr = cdr;
    return v;
}

Value* mk_prim(PrimFn fn) {
    Value* v = alloc_val(T_PRIM);
    if (!v) return NULL;
    v->prim = fn;
    return v;
}

Value* mk_code(const char* s) {
    if (!s) s = "";
    Value* v = alloc_val(T_CODE);
    if (!v) return NULL;
    v->s = s ? strdup(s) : NULL;
    if (s && !v->s) {
        // Don't free v if using arena (arena will bulk free)
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (v->s && compiler_arena_current) {
        compiler_arena_register_string(v->s);
    }
    return v;
}

Value* mk_lambda(Value* params, Value* body, Value* env) {
    Value* v = alloc_val(T_LAMBDA);
    if (!v) return NULL;
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
    if (!s1->s || !s2->s) return 0;
    return strcmp(s1->s, s2->s) == 0;
}

int sym_eq_str(Value* s1, const char* s2) {
    if (!s1 || s1->tag != T_SYM) return 0;
    if (!s1->s || !s2) return 0;
    return strcmp(s1->s, s2) == 0;
}

char* list_to_str(Value* v) {
    DString* ds = ds_new();
    if (!ds) return NULL;
    ds_append_char(ds, '(');
    while (v && !is_nil(v)) {
        char* s = val_to_str(car(v));
        if (s) {
            ds_append(ds, s);
            free(s);
        }
        v = cdr(v);
        if (v && !is_nil(v)) ds_append_char(ds, ' ');
    }
    ds_append_char(ds, ')');
    return ds_take(ds);
}

char* val_to_str(Value* v) {
    if (!v) return strdup("NULL");
    DString* ds;
    switch (v->tag) {
        case T_INT:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append_int(ds, v->i);
            return ds_take(ds);
        case T_SYM:
            return v->s ? strdup(v->s) : NULL;
        case T_CODE:
            return v->s ? strdup(v->s) : NULL;
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
