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
    v->s = strdup(s);
    if (!v->s) {
        // Don't free v if using arena (arena will bulk free)
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (compiler_arena_current) {
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
    v->s = strdup(s);
    if (!v->s) {
        // Don't free v if using arena (arena will bulk free)
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (compiler_arena_current) {
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

Value* mk_error(const char* msg) {
    if (!msg) msg = "unknown error";
    Value* v = alloc_val(T_ERROR);
    if (!v) return NULL;
    v->s = strdup(msg);
    if (!v->s) {
        if (!compiler_arena_current) free(v);
        return NULL;
    }
    if (compiler_arena_current) {
        compiler_arena_register_string(v->s);
    }
    return v;
}

Value* mk_box(Value* initial) {
    Value* v = alloc_val(T_BOX);
    if (!v) return NULL;
    v->box_value = initial;
    return v;
}

Value* mk_cont(ContFn fn, Value* menv, int tag) {
    Value* v = alloc_val(T_CONT);
    if (!v) return NULL;
    v->cont.fn = fn;
    v->cont.menv = menv;
    v->cont.tag = tag;
    return v;
}

Value* mk_chan(int capacity) {
    Value* v = alloc_val(T_CHAN);
    if (!v) return NULL;

    // Allocate the Channel structure
    Channel* ch = malloc(sizeof(Channel));
    if (!ch) {
        if (!compiler_arena_current) free(v);
        return NULL;
    }

    ch->capacity = capacity;
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    ch->closed = 0;
    ch->send_waiters = NULL;
    ch->recv_waiters = NULL;

    // Allocate buffer for buffered channels
    if (capacity > 0) {
        ch->buffer = malloc(sizeof(Value*) * capacity);
        if (!ch->buffer) {
            free(ch);
            if (!compiler_arena_current) free(v);
            return NULL;
        }
        for (int i = 0; i < capacity; i++) {
            ch->buffer[i] = NULL;
        }
    } else {
        ch->buffer = NULL;
    }

    v->chan.ch = ch;
    v->chan.capacity = capacity;
    return v;
}

Value* mk_process(Value* thunk) {
    Value* v = alloc_val(T_PROCESS);
    if (!v) return NULL;
    v->proc.thunk = thunk;
    v->proc.cont = NULL;
    v->proc.menv = NULL;
    v->proc.result = NULL;
    v->proc.park_value = NULL;
    v->proc.state = PROC_READY;
    return v;
}

// -- Type Predicates --

int is_box(Value* v) {
    return v != NULL && v->tag == T_BOX;
}

int is_cont(Value* v) {
    return v != NULL && v->tag == T_CONT;
}

int is_chan(Value* v) {
    return v != NULL && v->tag == T_CHAN;
}

int is_process(Value* v) {
    return v != NULL && v->tag == T_PROCESS;
}

int is_error(Value* v) {
    return v != NULL && v->tag == T_ERROR;
}

// -- Box Operations --

Value* box_get(Value* box) {
    if (!box || box->tag != T_BOX) return NULL;
    return box->box_value;
}

void box_set(Value* box, Value* val) {
    if (box && box->tag == T_BOX) {
        box->box_value = val;
    }
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
        case T_ERROR:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append(ds, "#<error: ");
            if (v->s) ds_append(ds, v->s);
            ds_append(ds, ">");
            return ds_take(ds);
        case T_BOX:
            ds = ds_new();
            if (!ds) return NULL;
            ds_append(ds, "#<box ");
            if (v->box_value) {
                char* inner = val_to_str(v->box_value);
                if (inner) {
                    ds_append(ds, inner);
                    free(inner);
                }
            } else {
                ds_append(ds, "nil");
            }
            ds_append(ds, ">");
            return ds_take(ds);
        case T_CONT:
            return strdup("#<continuation>");
        case T_CHAN:
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<channel cap=%d>", v->chan.capacity);
            return ds_take(ds);
        case T_PROCESS: {
            const char* state_names[] = {"ready", "running", "parked", "done"};
            const char* state = (v->proc.state >= 0 && v->proc.state <= 3)
                                ? state_names[v->proc.state] : "unknown";
            ds = ds_new();
            if (!ds) return NULL;
            ds_printf(ds, "#<process %s>", state);
            return ds_take(ds);
        }
        default:
            return strdup("?");
    }
}
