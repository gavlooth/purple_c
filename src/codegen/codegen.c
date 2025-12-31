#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "../memory/scc.h"
#include "../memory/deferred.h"
#include "../util/dstring.h"
#include <stdio.h>
#include <string.h>

// -- Globals --
TypeDef* TYPE_REGISTRY = NULL;
OwnershipEdge* OWNERSHIP_GRAPH = NULL;

// Visit states for DFS
typedef struct VisitState {
    char* type_name;
    int color;  // 0=white, 1=gray, 2=black
    struct VisitState* next;
} VisitState;

static VisitState* VISIT_STATES = NULL;

// -- Code Emission --

static int val_to_c_expr_rec(Value* v, DString* ds) {
    if (!v || v->tag == T_NIL) {
        ds_append(ds, "NULL");
        return 1;
    }
    switch (v->tag) {
        case T_CODE:
            ds_append(ds, v->s);
            return 1;
        case T_INT:
            ds_printf(ds, "mk_int(%ld)", v->i);
            return 1;
        case T_CELL:
            ds_append(ds, "mk_pair(");
            if (!val_to_c_expr_rec(car(v), ds)) return 0;
            ds_append(ds, ", ");
            if (!val_to_c_expr_rec(cdr(v), ds)) return 0;
            ds_append(ds, ")");
            return 1;
        default:
            return 0;
    }
}

char* val_to_c_expr(Value* v) {
    DString* ds = ds_new();
    if (!ds) return NULL;
    if (!val_to_c_expr_rec(v, ds)) {
        ds_free(ds);
        return NULL;
    }
    return ds_take(ds);
}

Value* emit_c_call(const char* fn, Value* a, Value* b) {
    char* sa = val_to_c_expr(a);
    char* sb = val_to_c_expr(b);

    if (!sa || !sb) {
        fprintf(stderr, "Error: cannot emit C for non-literal argument\n");
        free(sa);
        free(sb);
        return mk_code("mk_int(0)");
    }

    DString* ds = ds_new();
    ds_printf(ds, "%s(%s, %s)", fn, sa, sb);

    free(sa);
    free(sb);
    return mk_code(ds_take(ds));
}

Value* lift_value(Value* v) {
    if (!v) return NULL;
    if (v->tag == T_CODE) return v;
    if (v->tag == T_INT) {
        DString* ds = ds_new();
        ds_printf(ds, "mk_int(%ld)", v->i);
        return mk_code(ds_take(ds));
    }
    return v;
}

// -- ASAP Scanner Generation --

void gen_asap_scanner(const char* type_name, int is_list) {
    printf("\n// [ASAP] Type-Aware Scanner for %s\n", type_name);
    printf("// Note: ASAP uses compile-time free injection, not runtime GC\n");
    printf("void scan_%s(Obj* x) {\n", type_name);
    printf("  if (!x || x->scan_tag) return;\n");
    printf("  x->scan_tag = 1;\n");
    if (is_list) {
        printf("  if (x->is_pair) {\n");
        printf("    scan_%s(x->a);\n", type_name);
        printf("    scan_%s(x->b);\n", type_name);
        printf("  }\n");
    }
    printf("}\n\n");

    printf("void clear_marks_%s(Obj* x) {\n", type_name);
    printf("  if (!x || !x->scan_tag) return;\n");
    printf("  x->scan_tag = 0;\n");
    if (is_list) {
        printf("  if (x->is_pair) {\n");
        printf("    clear_marks_%s(x->a);\n", type_name);
        printf("    clear_marks_%s(x->b);\n", type_name);
        printf("  }\n");
    }
    printf("}\n");
}

// -- Type Registry --

void register_type(const char* name, TypeField* fields, int count) {
    if (!name) return;
    TypeDef* t = malloc(sizeof(TypeDef));
    if (!t) return;
    t->name = strdup(name);
    if (!t->name) {
        free(t);
        return;
    }
    t->fields = fields;
    t->field_count = count;
    t->is_recursive = 0;
    t->next = TYPE_REGISTRY;
    TYPE_REGISTRY = t;

    for (int i = 0; i < count; i++) {
        if (fields[i].is_scannable) {
            fields[i].strength = FIELD_STRONG;
            if (strcmp(fields[i].type, name) == 0) {
                t->is_recursive = 1;
            }
        } else {
            fields[i].strength = FIELD_UNTRACED;
        }
    }
}

TypeDef* find_type(const char* name) {
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    return NULL;
}

// -- Ownership Graph --

void build_ownership_graph(void) {
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        for (int i = 0; i < t->field_count; i++) {
            if (t->fields[i].is_scannable) {
                OwnershipEdge* e = malloc(sizeof(OwnershipEdge));
                if (!e) continue;
                e->from_type = strdup(t->name);
                e->field_name = strdup(t->fields[i].name);
                e->to_type = strdup(t->fields[i].type);
                if (!e->from_type || !e->field_name || !e->to_type) {
                    free(e->from_type);
                    free(e->field_name);
                    free(e->to_type);
                    free(e);
                    continue;
                }
                e->is_back_edge = 0;
                e->next = OWNERSHIP_GRAPH;
                OWNERSHIP_GRAPH = e;
            }
        }
        t = t->next;
    }
}

static VisitState* find_visit_state(const char* name) {
    VisitState* v = VISIT_STATES;
    while (v) {
        if (strcmp(v->type_name, name) == 0) return v;
        v = v->next;
    }
    return NULL;
}

static void add_visit_state(const char* name, int color) {
    VisitState* existing = find_visit_state(name);
    if (existing) {
        existing->color = color;
        return;
    }
    VisitState* v = malloc(sizeof(VisitState));
    if (!v) return;
    v->type_name = strdup(name);
    if (!v->type_name) {
        free(v);
        return;
    }
    v->color = color;
    v->next = VISIT_STATES;
    VISIT_STATES = v;
}

static void mark_field_weak(const char* type_name, const char* field_name) {
    TypeDef* t = find_type(type_name);
    if (!t) return;

    for (int i = 0; i < t->field_count; i++) {
        if (strcmp(t->fields[i].name, field_name) == 0) {
            t->fields[i].strength = FIELD_WEAK;
            printf("// AUTO-WEAK: %s.%s\n", type_name, field_name);
            return;
        }
    }
}

static void detect_back_edges_dfs(const char* type_name, const char*** path, int* path_len, int* path_cap) {
    VisitState* v = find_visit_state(type_name);

    if (v && v->color == 1) return;
    if (v && v->color == 2) return;

    add_visit_state(type_name, 1);

    // Ensure path capacity
    if (*path_len >= *path_cap) {
        *path_cap *= 2;
        *path = realloc(*path, (*path_cap) * sizeof(char*));
        if (!*path) return; // Allocation failed
    }

    (*path)[*path_len] = type_name;
    (*path_len)++;

    OwnershipEdge* e = OWNERSHIP_GRAPH;
    while (e) {
        if (strcmp(e->from_type, type_name) == 0) {
            for (int i = 0; i < *path_len; i++) {
                if (strcmp((*path)[i], e->to_type) == 0) {
                    e->is_back_edge = 1;
                    mark_field_weak(e->from_type, e->field_name);
                }
            }
            detect_back_edges_dfs(e->to_type, path, path_len, path_cap);
        }
        e = e->next;
    }

    (*path_len)--;
    add_visit_state(type_name, 2);
}

void analyze_back_edges(void) {
    int path_cap = 256;
    int path_len = 0;
    const char** path = malloc(path_cap * sizeof(char*));
    if (!path) return;

    VISIT_STATES = NULL;

    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        if (find_visit_state(t->name) == NULL) {
            detect_back_edges_dfs(t->name, &path, &path_len, &path_cap);
        }
        t = t->next;
    }

    free(path);
}

// -- Field-Aware Scanner --

void gen_field_aware_scanner(const char* type_name) {
    TypeDef* t = find_type(type_name);
    if (!t) {
        gen_asap_scanner(type_name, 1);
        return;
    }

    printf("\n// [ASAP] Field-Aware Scanner for %s\n", type_name);
    printf("void scan_%s(%s* x) {\n", type_name, type_name);
    printf("  if (!x || x->scan_tag) return;\n");
    printf("  x->scan_tag = 1;\n");

    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable && t->fields[i].strength == FIELD_STRONG) {
            printf("  scan_%s(x->%s);\n", t->fields[i].type, t->fields[i].name);
        }
    }

    printf("}\n");
}

// -- Struct Generation --

void gen_struct_def(TypeDef* t) {
    printf("typedef struct %s {\n", t->name);
    printf("    int _rc;\n");
    printf("    int _weak_rc;\n");
    printf("    unsigned int scan_tag; // Scanner mark\n");

    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable) {
            if (t->fields[i].strength == FIELD_WEAK) {
                printf("    WeakRef* %s;  // WEAK\n", t->fields[i].name);
            } else {
                printf("    struct %s* %s;  // STRONG\n",
                       t->fields[i].type, t->fields[i].name);
            }
        } else {
            printf("    int %s;  // VALUE\n", t->fields[i].name);
        }
    }

    printf("} %s;\n\n", t->name);
}

void gen_release_func(TypeDef* t) {
    printf("void release_%s(%s* obj) {\n", t->name, t->name);
    printf("    if (!obj) return;\n");
    printf("    obj->_rc--;\n");
    printf("    if (obj->_rc == 0) {\n");

    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable && t->fields[i].strength == FIELD_STRONG) {
            printf("        release_%s(obj->%s);\n", t->fields[i].type, t->fields[i].name);
        }
    }

    printf("        if (obj->_weak_rc == 0) {\n");
    printf("            free(obj);\n");
    printf("        } else {\n");
    printf("            obj->_rc = -1;\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");
}

// -- Weak Reference Runtime --

void gen_weak_ref_runtime(void) {
    printf("// Phase 3: Weak Reference Support\n");
    printf("typedef struct WeakRef {\n");
    printf("    void* target;\n");
    printf("    int alive;\n");
    printf("} WeakRef;\n\n");

    printf("typedef struct WeakRefNode {\n");
    printf("    WeakRef* ref;\n");
    printf("    struct WeakRefNode* next;\n");
    printf("} WeakRefNode;\n\n");

    printf("WeakRefNode* WEAK_REF_HEAD = NULL;\n\n");

    printf("WeakRef* mk_weak_ref(void* target) {\n");
    printf("    WeakRef* w = malloc(sizeof(WeakRef));\n");
    printf("    w->target = target;\n");
    printf("    w->alive = 1;\n");
    printf("    WeakRefNode* node = malloc(sizeof(WeakRefNode));\n");
    printf("    node->ref = w;\n");
    printf("    node->next = WEAK_REF_HEAD;\n");
    printf("    WEAK_REF_HEAD = node;\n");
    printf("    return w;\n");
    printf("}\n\n");

    printf("void* deref_weak(WeakRef* w) {\n");
    printf("    if (w && w->alive) return w->target;\n");
    printf("    return NULL;\n");
    printf("}\n\n");

    printf("void invalidate_weak(WeakRef* w) {\n");
    printf("    if (w) w->alive = 0;\n");
    printf("}\n\n");

    printf("void invalidate_weak_refs_for(void* target) {\n");
    printf("    WeakRefNode* n = WEAK_REF_HEAD;\n");
    printf("    while (n) {\n");
    printf("        WeakRef* obj = n->ref;\n");
    printf("        if (obj->target == target) {\n");
    printf("            invalidate_weak(obj);\n");
    printf("        }\n");
    printf("        n = n->next;\n");
    printf("    }\n");
    printf("}\n\n");
}

// -- Perceus Runtime --

void gen_perceus_runtime(void) {
    printf("// Phase 4: Perceus Reuse Analysis Runtime\n\n");

    printf("Obj* try_reuse(Obj* old, size_t size) {\n");
    printf("    if (old && old->mark == 1) {\n");
    printf("        // Reusing: release children if this was a pair\n");
    printf("        if (old->is_pair) {\n");
    printf("            if (old->a) dec_ref(old->a);\n");
    printf("            if (old->b) dec_ref(old->b);\n");
    printf("            old->a = NULL;\n");
    printf("            old->b = NULL;\n");
    printf("        }\n");
    printf("        return old;\n");
    printf("    }\n");
    printf("    if (old) dec_ref(old);\n");
    printf("    return malloc(size);\n");
    printf("}\n\n");

    printf("Obj* reuse_as_int(Obj* old, long value) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    obj->mark = 1;\n");
    printf("    obj->scc_id = -1;\n");
    printf("    obj->is_pair = 0;\n");
    printf("    obj->scan_tag = 0;\n");
    printf("    obj->i = value;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    obj->mark = 1;\n");
    printf("    obj->scc_id = -1;\n");
    printf("    obj->is_pair = 1;\n");
    printf("    obj->scan_tag = 0;\n");
    printf("    obj->a = a;\n");
    printf("    obj->b = b;\n");
    printf("    return obj;\n");
    printf("}\n\n");
}

// -- NLL Free Generation --

void gen_nll_free(FreePoint* fp, char* buf, int buf_size) {
    if (fp->is_conditional) {
        snprintf(buf, buf_size,
            "  // NLL: %s may be freed here on some paths\n"
            "  if (!_path_uses_%s) free_obj(%s);\n",
            fp->var_name, fp->var_name, fp->var_name);
    } else {
        snprintf(buf, buf_size,
            "  // NLL: %s freed early (before scope end)\n"
            "  free_obj(%s);\n",
            fp->var_name, fp->var_name);
    }
}

// -- Type Registry Init --

void init_type_registry(void) {
    static TypeField pair_fields[] = {
        {"a", "Obj", 1, FIELD_STRONG},
        {"b", "Obj", 1, FIELD_STRONG}
    };
    register_type("Pair", pair_fields, 2);

    static TypeField list_fields[] = {
        {"a", "List", 1, FIELD_STRONG},
        {"b", "List", 1, FIELD_STRONG}
    };
    register_type("List", list_fields, 2);

    static TypeField tree_fields[] = {
        {"left", "Tree", 1, FIELD_STRONG},
        {"right", "Tree", 1, FIELD_STRONG},
        {"value", "int", 0, FIELD_UNTRACED}
    };
    register_type("Tree", tree_fields, 3);

    build_ownership_graph();
    analyze_back_edges();
}

// -- Runtime Header Generation --

void gen_runtime_header(void) {
    printf("// Purple + ASAP C Compiler Output\n");
    printf("// Primary Strategy: ASAP + ISMM 2024 (Deeply Immutable Cycles)\n\n");

    printf("#include <stdlib.h>\n");
    printf("#include <stdio.h>\n");
    printf("#include <limits.h>\n");
    printf("#include <stdint.h>\n\n");
    printf("void invalidate_weak_refs_for(void* target);\n\n");

    printf("typedef struct Obj {\n");
    printf("    int mark;      // Reference count or mark bit\n");
    printf("    int scc_id;    // SCC identifier (-1 if not in SCC)\n");
    printf("    int is_pair;   // 1 if pair, 0 if int\n");
    printf("    unsigned int scan_tag; // Scanner mark (separate from RC)\n");
    printf("    union {\n");
    printf("        long i;\n");
    printf("        struct { struct Obj *a, *b; };\n");
    printf("    };\n");
    printf("} Obj;\n\n");

    // Dynamic free list
    printf("// Dynamic Free List\n");
    printf("typedef struct FreeNode { Obj* obj; struct FreeNode* next; } FreeNode;\n");
    printf("FreeNode* FREE_HEAD = NULL;\n");
    printf("int FREE_COUNT = 0;\n\n");

    // Stack pool
    printf("// Stack Allocation Pool\n");
    printf("#define STACK_POOL_SIZE 256\n");
    printf("Obj STACK_POOL[STACK_POOL_SIZE];\n");
    printf("int STACK_PTR = 0;\n\n");

    printf("static int is_stack_obj(Obj* x) {\n");
    printf("    uintptr_t px = (uintptr_t)x;\n");
    printf("    uintptr_t start = (uintptr_t)&STACK_POOL[0];\n");
    printf("    uintptr_t end = (uintptr_t)&STACK_POOL[STACK_POOL_SIZE];\n");
    printf("    return px >= start && px < end;\n");
    printf("}\n\n");

    // Constructors
    printf("Obj* mk_int(long i) {\n");
    printf("    Obj* x = malloc(sizeof(Obj));\n");
    printf("    if (!x) return NULL;\n");
    printf("    x->mark = 1; x->scc_id = -1; x->is_pair = 0; x->scan_tag = 0;\n");
    printf("    x->i = i;\n");
    printf("    return x;\n");
    printf("}\n\n");

    printf("Obj* mk_pair(Obj* a, Obj* b) {\n");
    printf("    Obj* x = malloc(sizeof(Obj));\n");
    printf("    if (!x) return NULL;\n");
    printf("    x->mark = 1; x->scc_id = -1; x->is_pair = 1; x->scan_tag = 0;\n");
    printf("    x->a = a; x->b = b;\n");
    printf("    return x;\n");
    printf("}\n\n");

    // Shape-based deallocation
    printf("// Phase 2: Shape-based deallocation (Ghiya-Hendren analysis)\n");
    printf("// TREE: Direct free (ASAP)\n");
    printf("void free_tree(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (is_stack_obj(x)) return;\n");
    printf("    if (x->is_pair) {\n");
        printf("        free_tree(x->a);\n");
        printf("        free_tree(x->b);\n");
    printf("    }\n");
    printf("    invalidate_weak_refs_for(x);\n");
    printf("    free(x);\n");
    printf("}\n\n");

    printf("// DAG: Reference counting\n");
    printf("void dec_ref(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (is_stack_obj(x)) return;\n");
    printf("    if (x->mark < 0) return;\n");
    printf("    x->mark--;\n");
    printf("    if (x->mark <= 0) {\n");
    printf("        if (x->is_pair) {\n");
    printf("            dec_ref(x->a);\n");
    printf("            dec_ref(x->b);\n");
    printf("        }\n");
    printf("        invalidate_weak_refs_for(x);\n");
    printf("        free(x);\n");
    printf("    }\n");
    printf("}\n\n");

    printf("void inc_ref(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (is_stack_obj(x)) return;\n");
    printf("    if (x->mark < 0) { x->mark = 1; return; }\n");
    printf("    x->mark++;\n");
    printf("}\n\n");

    // Free list operations
    printf("void free_obj(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (is_stack_obj(x)) return;\n");
    printf("    if (x->mark < 0) return;\n");
    printf("    x->mark = -1;\n");
    printf("    FreeNode* n = malloc(sizeof(FreeNode));\n");
    printf("    n->obj = x; n->next = FREE_HEAD; FREE_HEAD = n;\n");
    printf("    FREE_COUNT++;\n");
    printf("}\n\n");

    printf("void flush_freelist() {\n");
    printf("    while (FREE_HEAD) {\n");
    printf("        FreeNode* n = FREE_HEAD;\n");
    printf("        FREE_HEAD = n->next;\n");
    printf("        if (n->obj->mark < 0) {\n");
    printf("            invalidate_weak_refs_for(n->obj);\n");
    printf("            free(n->obj);\n");
    printf("        }\n");
    printf("        free(n);\n");
    printf("    }\n");
    printf("    FREE_COUNT = 0;\n");
    printf("}\n\n");

    // Stack allocation
    printf("Obj* mk_int_stack(long i) {\n");
    printf("    if (STACK_PTR < STACK_POOL_SIZE) {\n");
    printf("        Obj* x = &STACK_POOL[STACK_PTR++];\n");
    printf("        x->mark = 0; x->scc_id = -1; x->is_pair = 0; x->scan_tag = 0;\n");
    printf("        x->i = i;\n");
    printf("        return x;\n");
    printf("    }\n");
    printf("    return mk_int(i);\n");
    printf("}\n\n");
}
