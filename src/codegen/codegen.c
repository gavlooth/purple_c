#define _POSIX_C_SOURCE 200809L
#include "codegen.h"
#include "../memory/scc.h"
#include "../memory/deferred.h"
#include "../util/dstring.h"
#include <limits.h>
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

// -- Back-Edge Naming Heuristics (from Go implementation) --
// Field names that suggest back-edges in ownership cycles
static const char* BACK_EDGE_HINTS[] = {
    "parent", "owner", "container",  // Points to ancestor/owner
    "prev", "previous", "back",      // Reverse direction in sequences
    "up", "outer",                   // Hierarchical back-references
    NULL
};

// Check if a field name matches a back-edge naming pattern
static int is_back_edge_hint(const char* field_name) {
    if (!field_name) return 0;

    // Convert to lowercase for comparison
    char lower[256];
    int i;
    for (i = 0; field_name[i] && i < 255; i++) {
        lower[i] = (field_name[i] >= 'A' && field_name[i] <= 'Z')
                   ? field_name[i] + 32 : field_name[i];
    }
    lower[i] = '\0';

    for (int j = 0; BACK_EDGE_HINTS[j]; j++) {
        if (strstr(lower, BACK_EDGE_HINTS[j])) {
            return 1;
        }
    }
    return 0;
}

// Check if type already has a weak pointer to target_type
static int has_weak_to(TypeDef* t, const char* target_type) {
    for (int i = 0; i < t->field_count; i++) {
        if (t->fields[i].is_scannable &&
            t->fields[i].strength == FIELD_WEAK &&
            strcmp(t->fields[i].type, target_type) == 0) {
            return 1;
        }
    }
    return 0;
}

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
    char* code_str = ds_take(ds);
    Value* result = mk_code(code_str);
    free(code_str);
    return result;
}

Value* lift_value(Value* v) {
    if (!v) return NULL;
    if (v->tag == T_CODE) return v;
    if (v->tag == T_INT) {
        DString* ds = ds_new();
        ds_printf(ds, "mk_int(%ld)", v->i);
        char* code_str = ds_take(ds);
        Value* result = mk_code(code_str);
        free(code_str);
        return result;
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

// Phase 1: Apply naming heuristics to mark obvious back-edges
static void apply_naming_heuristics(void) {
    printf("// Phase 1: Applying naming heuristics for back-edge detection\n");

    OwnershipEdge* e = OWNERSHIP_GRAPH;
    while (e) {
        if (is_back_edge_hint(e->field_name)) {
            e->is_back_edge = 1;
            mark_field_weak(e->from_type, e->field_name);
        }
        e = e->next;
    }
}

// Phase 2: Detect second pointers to same type
// Example: Node has (next Node) and (prev Node) - if prev wasn't caught by naming, mark it weak
static void detect_second_pointers(void) {
    printf("// Phase 2: Detecting second pointers to same type\n");

    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        // Track first strong pointer to each target type
        // For simplicity, use linear scan (types typically have few fields)
        char* first_target[64];   // target type
        int first_count = 0;

        for (int i = 0; i < t->field_count && first_count < 64; i++) {
            TypeField* f = &t->fields[i];
            if (!f->is_scannable || f->strength == FIELD_WEAK) continue;

            // If there's already a weak pointer to this type, skip
            if (has_weak_to(t, f->type)) continue;

            // Check if we've seen this target type before
            int found = 0;
            for (int j = 0; j < first_count; j++) {
                if (strcmp(first_target[j], f->type) == 0) {
                    // Second pointer to same type - mark it weak
                    f->strength = FIELD_WEAK;
                    printf("// AUTO-WEAK (second pointer): %s.%s\n", t->name, f->name);

                    // Update ownership graph
                    OwnershipEdge* e = OWNERSHIP_GRAPH;
                    while (e) {
                        if (strcmp(e->from_type, t->name) == 0 &&
                            strcmp(e->field_name, f->name) == 0) {
                            e->is_back_edge = 1;
                            break;
                        }
                        e = e->next;
                    }
                    found = 1;
                    break;
                }
            }

            if (!found) {
                // First pointer to this type - record it
                first_target[first_count] = f->type;
                first_count++;
            }
        }
        t = t->next;
    }
}

// Check if cycle from 'from' to 'to' is already broken by existing back-edges
static int is_cycle_already_broken(const char* from_type, const char* to_type,
                                    const char** path, int path_len) {
    // For self-loops
    if (strcmp(from_type, to_type) == 0) {
        OwnershipEdge* e = OWNERSHIP_GRAPH;
        while (e) {
            if (strcmp(e->from_type, from_type) == 0 &&
                strcmp(e->to_type, to_type) == 0 &&
                e->is_back_edge) {
                return 1;
            }
            e = e->next;
        }
        return 0;
    }

    // For longer cycles, check if any edge on the path is already a back-edge
    for (int i = 0; i < path_len; i++) {
        if (strcmp(path[i], to_type) == 0) {
            // Found the cycle start, check edges from to_type onwards
            for (int j = i; j < path_len - 1; j++) {
                OwnershipEdge* e = OWNERSHIP_GRAPH;
                while (e) {
                    if (strcmp(e->from_type, path[j]) == 0 &&
                        strcmp(e->to_type, path[j + 1]) == 0 &&
                        e->is_back_edge) {
                        return 1;
                    }
                    e = e->next;
                }
            }
            break;
        }
    }
    return 0;
}

// Phase 3: DFS cycle detection (only marks if cycle not already broken)
static void detect_back_edges_dfs_v2(const char* type_name, const char*** path,
                                      int* path_len, int* path_cap) {
    VisitState* v = find_visit_state(type_name);

    if (v && v->color == 1) return;  // In current path
    if (v && v->color == 2) return;  // Already finished

    add_visit_state(type_name, 1);

    // Ensure path capacity
    if (*path_len >= *path_cap) {
        if (*path_cap > INT_MAX / 2) return;
        int new_cap = (*path_cap) * 2;
        const char** tmp = realloc(*path, new_cap * sizeof(char*));
        if (!tmp) return;
        *path = tmp;
        *path_cap = new_cap;
    }

    (*path)[*path_len] = type_name;
    (*path_len)++;

    OwnershipEdge* e = OWNERSHIP_GRAPH;
    while (e) {
        if (strcmp(e->from_type, type_name) == 0 && !e->is_back_edge) {
            // Check if target is in current path (potential cycle)
            int is_cycle = 0;
            for (int i = 0; i < *path_len; i++) {
                if (strcmp((*path)[i], e->to_type) == 0) {
                    is_cycle = 1;
                    break;
                }
            }

            if (is_cycle) {
                // Only mark as back-edge if cycle isn't already broken
                if (!is_cycle_already_broken(type_name, e->to_type, *path, *path_len)) {
                    e->is_back_edge = 1;
                    mark_field_weak(e->from_type, e->field_name);
                    printf("// AUTO-WEAK (DFS cycle): %s.%s\n", e->from_type, e->field_name);
                }
            } else {
                detect_back_edges_dfs_v2(e->to_type, path, path_len, path_cap);
            }
        }
        e = e->next;
    }

    (*path_len)--;
    add_visit_state(type_name, 2);
}

void analyze_back_edges(void) {
    printf("// === Three-Phase Back-Edge Detection ===\n");

    // Phase 1: Naming heuristics (prev, parent, owner, etc.)
    apply_naming_heuristics();

    // Phase 2: Second-pointer detection
    detect_second_pointers();

    // Phase 3: DFS-based cycle detection for remaining edges
    printf("// Phase 3: DFS cycle detection for remaining edges\n");

    int path_cap = 256;
    int path_len = 0;
    const char** path = malloc(path_cap * sizeof(char*));
    if (!path) return;

    // Free any existing visit states before resetting
    while (VISIT_STATES) {
        VisitState* next = VISIT_STATES->next;
        free(VISIT_STATES->type_name);
        free(VISIT_STATES);
        VISIT_STATES = next;
    }

    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        if (find_visit_state(t->name) == NULL) {
            detect_back_edges_dfs_v2(t->name, &path, &path_len, &path_cap);
        }
        t = t->next;
    }

    free(path);

    // Free visit states created during DFS
    while (VISIT_STATES) {
        VisitState* next = VISIT_STATES->next;
        free(VISIT_STATES->type_name);
        free(VISIT_STATES);
        VISIT_STATES = next;
    }

    printf("// === Back-Edge Detection Complete ===\n\n");
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
    printf("    if (!w) return NULL;\n");
    printf("    w->target = target;\n");
    printf("    w->alive = 1;\n");
    printf("    WeakRefNode* node = malloc(sizeof(WeakRefNode));\n");
    printf("    if (!node) { free(w); return NULL; }\n");
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
    printf("    WeakRefNode** prev = &WEAK_REF_HEAD;\n");
    printf("    while (*prev) {\n");
    printf("        WeakRefNode* n = *prev;\n");
    printf("        WeakRef* obj = n->ref;\n");
    printf("        if (obj->target == target) {\n");
    printf("            *prev = n->next;\n");
    printf("            free(obj);\n");
    printf("            free(n);\n");
    printf("        } else {\n");
    printf("            prev = &n->next;\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");

    // Add cleanup function for program end
    printf("void cleanup_all_weak_refs(void) {\n");
    printf("    while (WEAK_REF_HEAD) {\n");
    printf("        WeakRefNode* n = WEAK_REF_HEAD;\n");
    printf("        WEAK_REF_HEAD = n->next;\n");
    printf("        free(n->ref);\n");
    printf("        free(n);\n");
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
    printf("    if (!obj) return NULL;\n");
    printf("    obj->mark = 1;\n");
    printf("    obj->scc_id = -1;\n");
    printf("    obj->is_pair = 0;\n");
    printf("    obj->scan_tag = 0;\n");
    printf("    obj->i = value;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    if (!obj) return NULL;\n");
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

    // RC Optimization: Direct free for proven-unique references (Lobster-style)
    printf("/* RC Optimization: Direct free for proven-unique references */\n");
    printf("/* When compile-time analysis proves a reference is the only one, skip RC check */\n");
    printf("void free_unique(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (is_stack_obj(x)) return;\n");
    printf("    /* Proven unique at compile time - no RC check needed */\n");
    printf("    if (x->is_pair) {\n");
    printf("        /* Children might not be unique, use dec_ref for safety */\n");
    printf("        dec_ref(x->a);\n");
    printf("        dec_ref(x->b);\n");
    printf("    }\n");
    printf("    invalidate_weak_refs_for(x);\n");
    printf("    free(x);\n");
    printf("}\n\n");

    // Free list operations
    printf("void free_obj(Obj* x) {\n");
    printf("    if (!x) return;\n");
    printf("    if (is_stack_obj(x)) return;\n");
    printf("    if (x->mark < 0) return;\n");
    printf("    x->mark = -1;\n");
    printf("    FreeNode* n = malloc(sizeof(FreeNode));\n");
    printf("    if (!n) { invalidate_weak_refs_for(x); free(x); return; }\n");
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
