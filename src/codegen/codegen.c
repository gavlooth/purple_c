#include "codegen.h"
#include "../memory/scc.h"
#include "../memory/deferred.h"
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

Value* emit_c_call(const char* fn, Value* a, Value* b) {
    char buf[1024];
    char* sa = (a->tag == T_CODE) ? a->s : val_to_str(a);
    char* sb = (b->tag == T_CODE) ? b->s : val_to_str(b);

    char ca[512], cb[512];
    if (a->tag == T_INT) sprintf(ca, "mk_int(%ld)", a->i);
    else strcpy(ca, sa);

    if (b->tag == T_INT) sprintf(cb, "mk_int(%ld)", b->i);
    else strcpy(cb, sb);

    sprintf(buf, "%s(%s, %s)", fn, ca, cb);

    if (a->tag != T_CODE) free(sa);
    if (b->tag != T_CODE) free(sb);
    return mk_code(buf);
}

Value* lift_value(Value* v) {
    if (v->tag == T_CODE) return v;
    if (v->tag == T_INT) {
        char buf[64];
        sprintf(buf, "mk_int(%ld)", v->i);
        return mk_code(buf);
    }
    return v;
}

// -- ASAP Scanner Generation --

void gen_asap_scanner(const char* type_name, int is_list) {
    printf("\n// [ASAP] Type-Aware Scanner for %s\n", type_name);
    printf("// Note: ASAP uses compile-time free injection, not runtime GC\n");
    printf("void scan_%s(Obj* x) {\n", type_name);
    printf("  if (!x || x->mark) return;\n");
    printf("  x->mark = 1;\n");
    if (is_list) {
        printf("  scan_%s(x->a);\n", type_name);
        printf("  scan_%s(x->b);\n", type_name);
    }
    printf("}\n\n");

    printf("void clear_marks_%s(Obj* x) {\n", type_name);
    printf("  if (!x || !x->mark) return;\n");
    printf("  x->mark = 0;\n");
    if (is_list) {
        printf("  clear_marks_%s(x->a);\n", type_name);
        printf("  clear_marks_%s(x->b);\n", type_name);
    }
    printf("}\n");
}

// -- Type Registry --

void register_type(const char* name, TypeField* fields, int count) {
    TypeDef* t = malloc(sizeof(TypeDef));
    t->name = strdup(name);
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
                e->from_type = strdup(t->name);
                e->field_name = strdup(t->fields[i].name);
                e->to_type = strdup(t->fields[i].type);
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
    v->type_name = strdup(name);
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

static void detect_back_edges_dfs(const char* type_name, const char* path[], int path_len) {
    VisitState* v = find_visit_state(type_name);

    if (v && v->color == 1) return;
    if (v && v->color == 2) return;

    add_visit_state(type_name, 1);

    path[path_len] = type_name;
    path_len++;

    OwnershipEdge* e = OWNERSHIP_GRAPH;
    while (e) {
        if (strcmp(e->from_type, type_name) == 0) {
            for (int i = 0; i < path_len; i++) {
                if (strcmp(path[i], e->to_type) == 0) {
                    e->is_back_edge = 1;
                    mark_field_weak(e->from_type, e->field_name);
                }
            }
            detect_back_edges_dfs(e->to_type, path, path_len);
        }
        e = e->next;
    }

    add_visit_state(type_name, 2);
}

void analyze_back_edges(void) {
    const char* path[256];
    VISIT_STATES = NULL;

    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        if (find_visit_state(t->name) == NULL) {
            detect_back_edges_dfs(t->name, path, 0);
        }
        t = t->next;
    }
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
    printf("  if (!x || x->mark) return;\n");
    printf("  x->mark = 1;\n");

    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable) {
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

    printf("WeakRef* mk_weak_ref(void* target) {\n");
    printf("    WeakRef* w = malloc(sizeof(WeakRef));\n");
    printf("    w->target = target;\n");
    printf("    w->alive = 1;\n");
    printf("    return w;\n");
    printf("}\n\n");

    printf("void* deref_weak(WeakRef* w) {\n");
    printf("    if (w && w->alive) return w->target;\n");
    printf("    return NULL;\n");
    printf("}\n\n");

    printf("void invalidate_weak(WeakRef* w) {\n");
    printf("    if (w) w->alive = 0;\n");
    printf("}\n\n");
}

// -- Perceus Runtime --

void gen_perceus_runtime(void) {
    printf("// Phase 4: Perceus Reuse Analysis Runtime\n\n");

    printf("Obj* try_reuse(Obj* old, size_t size) {\n");
    printf("    if (old && old->mark == 1) {\n");
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
    printf("    obj->i = value;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    obj->mark = 1;\n");
    printf("    obj->scc_id = -1;\n");
    printf("    obj->is_pair = 1;\n");
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
    printf("#include <stdio.h>\n\n");

    printf("typedef struct Obj {\n");
    printf("    int mark;      // Reference count or mark bit\n");
    printf("    int scc_id;    // SCC identifier (-1 if not in SCC)\n");
    printf("    int is_pair;   // 1 if pair, 0 if int\n");
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

    // Constructors
    printf("Obj* mk_int(long i) {\n");
    printf("    Obj* x = malloc(sizeof(Obj));\n");
    printf("    x->mark = 1; x->scc_id = -1; x->is_pair = 0;\n");
    printf("    x->i = i;\n");
    printf("    return x;\n");
    printf("}\n\n");

    printf("Obj* mk_pair(Obj* a, Obj* b) {\n");
    printf("    Obj* x = malloc(sizeof(Obj));\n");
    printf("    x->mark = 1; x->scc_id = -1; x->is_pair = 1;\n");
    printf("    x->a = a; x->b = b;\n");
    printf("    return x;\n");
    printf("}\n\n");

    // Shape-based deallocation
    printf("// Phase 2: Shape-based deallocation (Ghiya-Hendren analysis)\n");
    printf("// TREE: Direct free (ASAP)\n");
    printf("void free_tree(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (x >= STACK_POOL && x < STACK_POOL + STACK_POOL_SIZE) return;\n");
    printf("    if (x->is_pair) {\n");
    printf("        free_tree(x->a);\n");
    printf("        free_tree(x->b);\n");
    printf("    }\n");
    printf("    free(x);\n");
    printf("}\n\n");

    printf("// DAG: Reference counting\n");
    printf("void dec_ref(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (x >= STACK_POOL && x < STACK_POOL + STACK_POOL_SIZE) return;\n");
    printf("    x->mark--;\n");
    printf("    if (x->mark <= 0) {\n");
    printf("        if (x->is_pair) {\n");
    printf("            dec_ref(x->a);\n");
    printf("            dec_ref(x->b);\n");
    printf("        }\n");
    printf("        free(x);\n");
    printf("    }\n");
    printf("}\n\n");

    // Free list operations
    printf("void free_obj(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (x >= STACK_POOL && x < STACK_POOL + STACK_POOL_SIZE) return;\n");
    printf("    FreeNode* n = malloc(sizeof(FreeNode));\n");
    printf("    n->obj = x; n->next = FREE_HEAD; FREE_HEAD = n;\n");
    printf("    FREE_COUNT++;\n");
    printf("}\n\n");

    printf("void flush_freelist() {\n");
    printf("    while (FREE_HEAD) {\n");
    printf("        FreeNode* n = FREE_HEAD;\n");
    printf("        FREE_HEAD = n->next;\n");
    printf("        free(n->obj); free(n);\n");
    printf("    }\n");
    printf("    FREE_COUNT = 0;\n");
    printf("}\n\n");

    // Stack allocation
    printf("Obj* mk_int_stack(long i) {\n");
    printf("    if (STACK_PTR < STACK_POOL_SIZE) {\n");
    printf("        Obj* x = &STACK_POOL[STACK_PTR++];\n");
    printf("        x->mark = 0; x->scc_id = -1; x->is_pair = 0;\n");
    printf("        x->i = i;\n");
    printf("        return x;\n");
    printf("    }\n");
    printf("    return mk_int(i);\n");
    printf("}\n\n");
}
