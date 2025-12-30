#include "scc.h"
#include <stdio.h>

// -- SCC Registry Management --

SCCRegistry* mk_scc_registry(void) {
    SCCRegistry* reg = malloc(sizeof(SCCRegistry));
    reg->sccs = NULL;
    reg->next_id = 1;
    reg->node_map = NULL;
    reg->stack = NULL;
    reg->index = 0;
    return reg;
}

void free_scc_registry(SCCRegistry* reg) {
    if (!reg) return;

    // Free all SCCs
    SCC* scc = reg->sccs;
    while (scc) {
        SCC* next = scc->next;
        if (scc->members) free(scc->members);
        free(scc);
        scc = next;
    }

    // Free node map
    SCCNode* node = reg->node_map;
    while (node) {
        SCCNode* next = node->next;
        free(node);
        node = next;
    }

    free(reg);
}

// -- SCC Operations --

SCC* create_scc(SCCRegistry* reg) {
    SCC* scc = malloc(sizeof(SCC));
    scc->id = reg->next_id++;
    scc->members = malloc(16 * sizeof(Obj*));
    scc->member_count = 0;
    scc->capacity = 16;
    scc->ref_count = 1;
    scc->frozen = 0;
    scc->next = reg->sccs;
    reg->sccs = scc;
    return scc;
}

void add_to_scc(SCC* scc, Obj* obj) {
    if (scc->member_count >= scc->capacity) {
        scc->capacity *= 2;
        scc->members = realloc(scc->members, scc->capacity * sizeof(Obj*));
    }
    scc->members[scc->member_count++] = obj;
    obj->scc_id = scc->id;
}

SCC* find_scc(SCCRegistry* reg, int scc_id) {
    SCC* scc = reg->sccs;
    while (scc) {
        if (scc->id == scc_id) return scc;
        scc = scc->next;
    }
    return NULL;
}

// -- Tarjan's Algorithm Helpers --

static SCCNode* get_node(SCCRegistry* reg, Obj* obj) {
    SCCNode* n = reg->node_map;
    while (n) {
        if (n->obj == obj) return n;
        n = n->next;
    }
    return NULL;
}

static SCCNode* get_or_create_node(SCCRegistry* reg, Obj* obj) {
    SCCNode* existing = get_node(reg, obj);
    if (existing) return existing;

    SCCNode* n = malloc(sizeof(SCCNode));
    n->id = -1;
    n->lowlink = -1;
    n->on_stack = 0;
    n->obj = obj;
    n->next = reg->node_map;
    n->stack_next = NULL;
    reg->node_map = n;
    return n;
}

static void push_stack(SCCRegistry* reg, SCCNode* node) {
    node->stack_next = reg->stack;
    reg->stack = node;
    node->on_stack = 1;
}

static SCCNode* pop_stack(SCCRegistry* reg) {
    if (!reg->stack) return NULL;
    SCCNode* node = reg->stack;
    reg->stack = node->stack_next;
    node->on_stack = 0;
    return node;
}

static int min(int a, int b) {
    return (a < b) ? a : b;
}

// -- Tarjan's SCC Algorithm --

void tarjan_dfs(SCCRegistry* reg, Obj* v, SCC** result) {
    if (!v) return;

    SCCNode* node = get_or_create_node(reg, v);

    // Skip if already processed
    if (node->id >= 0) return;

    node->id = node->lowlink = reg->index++;
    push_stack(reg, node);

    // Visit children (a and b pointers)
    if (v->a) {
        SCCNode* w_node = get_node(reg, v->a);
        if (!w_node || w_node->id < 0) {
            tarjan_dfs(reg, v->a, result);
            w_node = get_node(reg, v->a);
            if (w_node) {
                node->lowlink = min(node->lowlink, w_node->lowlink);
            }
        } else if (w_node->on_stack) {
            node->lowlink = min(node->lowlink, w_node->id);
        }
    }

    if (v->b) {
        SCCNode* w_node = get_node(reg, v->b);
        if (!w_node || w_node->id < 0) {
            tarjan_dfs(reg, v->b, result);
            w_node = get_node(reg, v->b);
            if (w_node) {
                node->lowlink = min(node->lowlink, w_node->lowlink);
            }
        } else if (w_node->on_stack) {
            node->lowlink = min(node->lowlink, w_node->id);
        }
    }

    // If v is root of SCC
    if (node->lowlink == node->id) {
        SCC* scc = create_scc(reg);
        SCCNode* w;
        do {
            w = pop_stack(reg);
            if (w) {
                add_to_scc(scc, w->obj);
            }
        } while (w && w != node);

        scc->next = *result;
        *result = scc;
    }
}

SCC* compute_sccs(SCCRegistry* reg, Obj* root) {
    reg->index = 0;
    SCC* result = NULL;
    tarjan_dfs(reg, root, &result);
    return result;
}

// -- Freeze Operations --

void inc_scc_ref(SCC* scc) {
    if (scc) scc->ref_count++;
}

void release_scc(SCC* scc) {
    if (!scc) return;
    scc->ref_count--;

    if (scc->ref_count == 0) {
        // Free all members
        for (int i = 0; i < scc->member_count; i++) {
            free(scc->members[i]);
        }
        free(scc->members);
        scc->members = NULL;
        scc->member_count = 0;
        // Note: SCC struct itself stays in registry until registry freed
    }
}

// -- Freeze Point Detection --

// Check if variable has no mutations after this point
static int has_no_mutations(const char* var, Value* expr) {
    if (!expr || is_nil(expr)) return 1;

    if (expr->tag == T_CELL) {
        Value* op = car(expr);
        Value* args = cdr(expr);

        // Check for set! on this variable
        if (op->tag == T_SYM && strcmp(op->s, "set!") == 0) {
            Value* target = car(args);
            if (target->tag == T_SYM && strcmp(target->s, var) == 0) {
                return 0;  // Found mutation
            }
        }

        // Check all subexpressions
        if (!has_no_mutations(var, op)) return 0;
        while (!is_nil(args)) {
            if (!has_no_mutations(var, car(args))) return 0;
            args = cdr(args);
        }
    }

    return 1;
}

int is_frozen_after_construction(const char* var, Value* body) {
    // A variable is "frozen" if there are no set! operations on it
    // after it's constructed
    return has_no_mutations(var, body);
}

FreezePoint* detect_freeze_points(Value* expr) {
    // TODO: Implement more sophisticated freeze point detection
    // For now, we detect explicit (freeze x) forms
    return NULL;
}

// -- Code Generation --

void gen_scc_runtime(void) {
    printf("\n// Phase 6b: SCC-based RC Runtime (ISMM 2024)\n");
    printf("// Reference Counting Deeply Immutable Data Structures with Cycles\n\n");

    printf("typedef struct SCC {\n");
    printf("    int id;\n");
    printf("    Obj** members;\n");
    printf("    int member_count;\n");
    printf("    int ref_count;\n");
    printf("    struct SCC* next;\n");
    printf("} SCC;\n\n");

    printf("SCC* SCC_REGISTRY[1024];\n");
    printf("int SCC_COUNT = 0;\n\n");

    printf("// Tarjan's algorithm for SCC computation\n");
    printf("typedef struct TarjanNode {\n");
    printf("    Obj* obj;\n");
    printf("    int index;\n");
    printf("    int lowlink;\n");
    printf("    int on_stack;\n");
    printf("} TarjanNode;\n\n");

    printf("TarjanNode* TARJAN_NODES[4096];\n");
    printf("int TARJAN_NODE_COUNT = 0;\n");
    printf("Obj* TARJAN_STACK[4096];\n");
    printf("int TARJAN_STACK_PTR = 0;\n");
    printf("int TARJAN_INDEX = 0;\n\n");

    printf("TarjanNode* get_tarjan_node(Obj* obj) {\n");
    printf("    for (int i = 0; i < TARJAN_NODE_COUNT; i++) {\n");
    printf("        if (TARJAN_NODES[i]->obj == obj) return TARJAN_NODES[i];\n");
    printf("    }\n");
    printf("    TarjanNode* n = malloc(sizeof(TarjanNode));\n");
    printf("    n->obj = obj;\n");
    printf("    n->index = -1;\n");
    printf("    n->lowlink = -1;\n");
    printf("    n->on_stack = 0;\n");
    printf("    TARJAN_NODES[TARJAN_NODE_COUNT++] = n;\n");
    printf("    return n;\n");
    printf("}\n\n");

    // Generate the actual tarjan_strongconnect implementation
    printf("void tarjan_strongconnect(Obj* v, SCC** result) {\n");
    printf("    if (!v) return;\n");
    printf("    TarjanNode* node = get_tarjan_node(v);\n");
    printf("    if (node->index >= 0) return; // Already visited\n");
    printf("    \n");
    printf("    node->index = TARJAN_INDEX;\n");
    printf("    node->lowlink = TARJAN_INDEX;\n");
    printf("    TARJAN_INDEX++;\n");
    printf("    TARJAN_STACK[TARJAN_STACK_PTR++] = v;\n");
    printf("    node->on_stack = 1;\n");
    printf("    \n");
    printf("    // Visit children (a and b fields)\n");
    printf("    if (v->a) {\n");
    printf("        TarjanNode* w_node = get_tarjan_node(v->a);\n");
    printf("        if (w_node->index < 0) {\n");
    printf("            tarjan_strongconnect(v->a, result);\n");
    printf("            if (node->lowlink > w_node->lowlink) node->lowlink = w_node->lowlink;\n");
    printf("        } else if (w_node->on_stack) {\n");
    printf("            if (node->lowlink > w_node->index) node->lowlink = w_node->index;\n");
    printf("        }\n");
    printf("    }\n");
    printf("    if (v->b) {\n");
    printf("        TarjanNode* w_node = get_tarjan_node(v->b);\n");
    printf("        if (w_node->index < 0) {\n");
    printf("            tarjan_strongconnect(v->b, result);\n");
    printf("            if (node->lowlink > w_node->lowlink) node->lowlink = w_node->lowlink;\n");
    printf("        } else if (w_node->on_stack) {\n");
    printf("            if (node->lowlink > w_node->index) node->lowlink = w_node->index;\n");
    printf("        }\n");
    printf("    }\n");
    printf("    \n");
    printf("    // If v is root of SCC\n");
    printf("    if (node->lowlink == node->index) {\n");
    printf("        SCC* scc = malloc(sizeof(SCC));\n");
    printf("        scc->id = SCC_COUNT;\n");
    printf("        scc->members = malloc(16 * sizeof(Obj*));\n");
    printf("        scc->member_count = 0;\n");
    printf("        scc->ref_count = 1;\n");
    printf("        \n");
    printf("        Obj* w;\n");
    printf("        do {\n");
    printf("            w = TARJAN_STACK[--TARJAN_STACK_PTR];\n");
    printf("            TarjanNode* w_node = get_tarjan_node(w);\n");
    printf("            w_node->on_stack = 0;\n");
    printf("            w->scc_id = scc->id;\n");
    printf("            scc->members[scc->member_count++] = w;\n");
    printf("        } while (w != v);\n");
    printf("        \n");
    printf("        // Link to result list (simplified)\n");
    printf("        SCC_REGISTRY[SCC_COUNT++] = scc;\n");
    printf("    }\n");
    printf("}\n\n");

    printf("SCC* freeze_cyclic(Obj* root) {\n");
    printf("    // Reset Tarjan state\n");
    printf("    TARJAN_NODE_COUNT = 0;\n");
    printf("    TARJAN_STACK_PTR = 0;\n");
    printf("    TARJAN_INDEX = 0;\n");
    printf("    \n");
    printf("    SCC* sccs = NULL;\n");
    printf("    tarjan_strongconnect(root, &sccs);\n");
    printf("    \n");
    printf("    // Register all SCCs\n");
    printf("    SCC* s = sccs;\n");
    printf("    while (s) {\n");
    printf("        SCC_REGISTRY[SCC_COUNT++] = s;\n");
    printf("        s = s->next;\n");
    printf("    }\n");
    printf("    \n");
    printf("    return sccs;\n");
    printf("}\n\n");

    printf("void release_scc(SCC* scc) {\n");
    printf("    if (!scc) return;\n");
    printf("    scc->ref_count--;\n");
    printf("    if (scc->ref_count == 0) {\n");
    printf("        for (int i = 0; i < scc->member_count; i++) {\n");
    printf("            free(scc->members[i]);\n");
    printf("        }\n");
    printf("        free(scc->members);\n");
    printf("        free(scc);\n");
    printf("    }\n");
    printf("}\n\n");

    printf("void inc_scc_ref(SCC* scc) {\n");
    printf("    if (scc) scc->ref_count++;\n");
    printf("}\n\n");
}

void gen_freeze_call(const char* var) {
    printf("    SCC* %s_scc = freeze_cyclic(%s);\n", var, var);
}

void gen_release_scc_call(const char* var) {
    printf("    release_scc(%s_scc);\n", var);
}
