#include "scc.h"
#include <stdio.h>
#include <limits.h>

// -- SCC Registry Management --

SCCRegistry* mk_scc_registry(void) {
    SCCRegistry* reg = malloc(sizeof(SCCRegistry));
    if (!reg) return NULL;
    reg->sccs = NULL;
    reg->next_id = 1;
    reg->node_map = NULL;
    reg->node_lookup = hashmap_new();
    if (!reg->node_lookup) {
        free(reg);
        return NULL;
    }
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

    // Free hash map
    if (reg->node_lookup) {
        hashmap_free(reg->node_lookup);
    }

    // Free node map (linked list for cleanup)
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
    if (!reg) return NULL;
    SCC* scc = malloc(sizeof(SCC));
    if (!scc) return NULL;
    scc->members = malloc(16 * sizeof(Obj*));
    if (!scc->members) {
        free(scc);
        return NULL;
    }
    scc->id = reg->next_id++;
    scc->member_count = 0;
    scc->capacity = 16;
    scc->ref_count = 1;
    scc->frozen = 0;
    // Link into registry list for cleanup (using 'next')
    scc->next = reg->sccs;
    reg->sccs = scc;
    // Separate field for result list (using 'result_next')
    scc->result_next = NULL;
    return scc;
}

void add_to_scc(SCC* scc, Obj* obj) {
    if (!scc || !obj) return;
    if (scc->member_count >= scc->capacity) {
        if (scc->capacity > INT_MAX / 2) return;  // Overflow protection
        int new_cap = scc->capacity * 2;
        Obj** new_members = realloc(scc->members, new_cap * sizeof(Obj*));
        if (!new_members) return;  // Keep existing data on failure
        scc->members = new_members;
        scc->capacity = new_cap;
    }
    scc->members[scc->member_count++] = obj;
    obj->scc_id = scc->id;
}

SCC* find_scc(SCCRegistry* reg, int scc_id) {
    if (!reg) return NULL;
    SCC* scc = reg->sccs;
    while (scc) {
        if (scc->id == scc_id) return scc;
        scc = scc->next;
    }
    return NULL;
}

// -- Tarjan's Algorithm Helpers --

// O(1) lookup using hash map
static SCCNode* get_node(SCCRegistry* reg, Obj* obj) {
    return (SCCNode*)hashmap_get(reg->node_lookup, obj);
}

static SCCNode* get_or_create_node(SCCRegistry* reg, Obj* obj) {
    if (!reg || !obj) return NULL;
    SCCNode* existing = get_node(reg, obj);
    if (existing) return existing;

    SCCNode* n = malloc(sizeof(SCCNode));
    if (!n) return NULL;
    n->id = -1;
    n->lowlink = -1;
    n->on_stack = 0;
    n->obj = obj;
    n->next = reg->node_map;
    n->stack_next = NULL;
    reg->node_map = n;

    // Add to hash map for O(1) lookup
    hashmap_put(reg->node_lookup, obj, n);

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

static void reset_tarjan_state(SCCRegistry* reg) {
    if (!reg) return;

    // Clear node map entries
    SCCNode* node = reg->node_map;
    while (node) {
        SCCNode* next = node->next;
        free(node);
        node = next;
    }
    reg->node_map = NULL;
    reg->stack = NULL;
    reg->index = 0;

    if (reg->node_lookup) {
        hashmap_free(reg->node_lookup);
        reg->node_lookup = NULL;
    }
    HashMap* new_map = hashmap_new();
    if (new_map) {
        reg->node_lookup = new_map;
    }
}

// -- Tarjan's SCC Algorithm (Iterative with explicit stack) --

// Work stack frame for iterative Tarjan's
typedef enum { TARJAN_INIT, TARJAN_AFTER_A, TARJAN_AFTER_B, TARJAN_DONE } TarjanState;

typedef struct TarjanFrame {
    Obj* v;
    SCCNode* node;
    TarjanState state;
    int pushed_a;  // Did we push a frame for child 'a'?
    int pushed_b;  // Did we push a frame for child 'b'?
    struct TarjanFrame* next;
} TarjanFrame;

static int push_tarjan_frame(TarjanFrame** work_stack, Obj* v, TarjanState state) {
    TarjanFrame* frame = malloc(sizeof(TarjanFrame));
    if (!frame) return 0;  // Allocation failed
    frame->v = v;
    frame->node = NULL;
    frame->state = state;
    frame->pushed_a = 0;
    frame->pushed_b = 0;
    frame->next = *work_stack;
    *work_stack = frame;
    return 1;  // Success
}

static TarjanFrame* pop_tarjan_frame(TarjanFrame** work_stack) {
    if (!*work_stack) return NULL;
    TarjanFrame* frame = *work_stack;
    *work_stack = frame->next;
    return frame;
}

void tarjan_dfs(SCCRegistry* reg, Obj* v, SCC** result) {
    if (!v) return;

    TarjanFrame* work_stack = NULL;
    push_tarjan_frame(&work_stack, v, TARJAN_INIT);

    while (work_stack) {
        TarjanFrame* frame = work_stack;
        Obj* curr = frame->v;

        switch (frame->state) {
        case TARJAN_INIT: {
            SCCNode* node = get_or_create_node(reg, curr);
            if (!node) {
                // Allocation failed - abort
                free(pop_tarjan_frame(&work_stack));
                break;
            }

            // Skip if already processed
            if (node->id >= 0) {
                free(pop_tarjan_frame(&work_stack));
                break;
            }

            node->id = node->lowlink = reg->index++;
            push_stack(reg, node);
            frame->node = node;

            // Process child 'a' first
            frame->state = TARJAN_AFTER_A;
            if (curr->is_pair && curr->a) {
                SCCNode* w_node = get_node(reg, curr->a);
                if (!w_node || w_node->id < 0) {
                    // Child not yet visited - push frame to process it
                    frame->pushed_a = 1;
                    push_tarjan_frame(&work_stack, curr->a, TARJAN_INIT);
                } else if (w_node->on_stack) {
                    // Child is on stack - update lowlink with its id
                    node->lowlink = min(node->lowlink, w_node->id);
                }
                // else: child already processed and not on stack - ignore
            }
            break;
        }

        case TARJAN_AFTER_A: {
            SCCNode* node = frame->node;

            // Update lowlink after processing 'a' (only if we pushed a frame)
            if (frame->pushed_a && curr->is_pair && curr->a) {
                SCCNode* w_node = get_node(reg, curr->a);
                if (w_node) {
                    node->lowlink = min(node->lowlink, w_node->lowlink);
                }
            }

            // Process child 'b'
            frame->state = TARJAN_AFTER_B;
            if (curr->is_pair && curr->b) {
                SCCNode* w_node = get_node(reg, curr->b);
                if (!w_node || w_node->id < 0) {
                    // Child not yet visited - push frame to process it
                    frame->pushed_b = 1;
                    push_tarjan_frame(&work_stack, curr->b, TARJAN_INIT);
                } else if (w_node->on_stack) {
                    // Child is on stack - update lowlink with its id
                    node->lowlink = min(node->lowlink, w_node->id);
                }
                // else: child already processed and not on stack - ignore
            }
            break;
        }

        case TARJAN_AFTER_B: {
            SCCNode* node = frame->node;

            // Update lowlink after processing 'b' (only if we pushed a frame)
            if (frame->pushed_b && curr->is_pair && curr->b) {
                SCCNode* w_node = get_node(reg, curr->b);
                if (w_node) {
                    node->lowlink = min(node->lowlink, w_node->lowlink);
                }
            }

            // If v is root of SCC
            if (node->lowlink == node->id) {
                SCC* scc = create_scc(reg);
                if (!scc) {
                    // Allocation failed - drain stack to avoid leaking state
                    SCCNode* w;
                    do {
                        w = pop_stack(reg);
                    } while (w && w != node);
                    free(pop_tarjan_frame(&work_stack));
                    break;
                }
                SCCNode* w;
                do {
                    w = pop_stack(reg);
                    if (w) {
                        add_to_scc(scc, w->obj);
                    }
                } while (w && w != node);

                scc->result_next = *result;
                *result = scc;
            }

            free(pop_tarjan_frame(&work_stack));
            break;
        }

        case TARJAN_DONE:
            free(pop_tarjan_frame(&work_stack));
            break;
        }
    }
}

SCC* compute_sccs(SCCRegistry* reg, Obj* root) {
    reset_tarjan_state(reg);
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
        if (op && op->tag == T_SYM && strcmp(op->s, "set!") == 0) {
            Value* target = car(args);
            if (target && target->tag == T_SYM && strcmp(target->s, var) == 0) {
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
    (void)expr;  // Not yet used - placeholder for future implementation
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
    printf("    int capacity;\n");
    printf("    int ref_count;\n");
    printf("    struct SCC* next;\n");
    printf("    struct SCC* result_next;\n");
    printf("} SCC;\n\n");

    printf("static int SCC_NEXT_ID = 0;\n\n");

    printf("// Tarjan's algorithm for SCC computation\n");
    printf("typedef struct TarjanNode {\n");
    printf("    Obj* obj;\n");
    printf("    int index;\n");
    printf("    int lowlink;\n");
    printf("    int on_stack;\n");
    printf("    struct TarjanNode* next;      // For linked list cleanup\n");
    printf("    struct TarjanNode* hash_next; // For hash bucket chain\n");
    printf("} TarjanNode;\n\n");

    printf("typedef struct TarjanStack {\n");
    printf("    Obj* obj;\n");
    printf("    struct TarjanStack* next;\n");
    printf("} TarjanStack;\n\n");

    printf("#define TARJAN_HASH_SIZE 1024\n");
    printf("TarjanNode* TARJAN_HASH[TARJAN_HASH_SIZE];\n");
    printf("TarjanNode* TARJAN_NODES = NULL;\n");
    printf("TarjanStack* TARJAN_STACK = NULL;\n");
    printf("int TARJAN_INDEX = 0;\n\n");
    printf("int TARJAN_OOM = 0;\n\n");

    printf("static size_t tarjan_hash_ptr(void* p) {\n");
    printf("    size_t x = (size_t)p;\n");
    printf("    x = ((x >> 16) ^ x) * 0x45d9f3b;\n");
    printf("    x = ((x >> 16) ^ x) * 0x45d9f3b;\n");
    printf("    return (x >> 16) ^ x;\n");
    printf("}\n\n");

    printf("TarjanNode* get_tarjan_node(Obj* obj) {\n");
    printf("    size_t idx = tarjan_hash_ptr(obj) %% TARJAN_HASH_SIZE;\n");
    printf("    TarjanNode* n = TARJAN_HASH[idx];\n");
    printf("    while (n) {\n");
    printf("        if (n->obj == obj) return n;\n");
    printf("        n = n->hash_next;\n");
    printf("    }\n");
    printf("    // Create new node\n");
    printf("    n = malloc(sizeof(TarjanNode));\n");
    printf("    if (!n) { TARJAN_OOM = 1; return NULL; }\n");
    printf("    n->obj = obj;\n");
    printf("    n->index = -1;\n");
    printf("    n->lowlink = -1;\n");
    printf("    n->on_stack = 0;\n");
    printf("    n->next = TARJAN_NODES;\n");
    printf("    n->hash_next = TARJAN_HASH[idx];\n");
    printf("    TARJAN_NODES = n;\n");
    printf("    TARJAN_HASH[idx] = n;\n");
    printf("    return n;\n");
    printf("}\n\n");

    printf("void tarjan_stack_push(Obj* obj) {\n");
    printf("    TarjanStack* s = malloc(sizeof(TarjanStack));\n");
    printf("    if (!s) { TARJAN_OOM = 1; return; }\n");
    printf("    s->obj = obj;\n");
    printf("    s->next = TARJAN_STACK;\n");
    printf("    TARJAN_STACK = s;\n");
    printf("}\n\n");

    printf("Obj* tarjan_stack_pop(void) {\n");
    printf("    if (!TARJAN_STACK) return NULL;\n");
    printf("    TarjanStack* s = TARJAN_STACK;\n");
    printf("    Obj* obj = s->obj;\n");
    printf("    TARJAN_STACK = s->next;\n");
    printf("    free(s);\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("void reset_tarjan_state(void) {\n");
    printf("    TarjanNode* n = TARJAN_NODES;\n");
    printf("    while (n) {\n");
    printf("        TarjanNode* next = n->next;\n");
    printf("        free(n);\n");
    printf("        n = next;\n");
    printf("    }\n");
    printf("    TARJAN_NODES = NULL;\n");
    printf("    // Clear hash table\n");
    printf("    for (int i = 0; i < TARJAN_HASH_SIZE; i++) TARJAN_HASH[i] = NULL;\n");
    printf("    while (TARJAN_STACK) {\n");
    printf("        TarjanStack* s = TARJAN_STACK;\n");
    printf("        TARJAN_STACK = s->next;\n");
    printf("        free(s);\n");
    printf("    }\n");
    printf("    TARJAN_INDEX = 0;\n");
    printf("    TARJAN_OOM = 0;\n");
    printf("}\n\n");

    // Generate iterative tarjan_strongconnect implementation
    printf("typedef enum { TARJAN_INIT, TARJAN_AFTER_A, TARJAN_AFTER_B, TARJAN_DONE } TarjanState;\n\n");
    
    printf("typedef struct TarjanWorkFrame {\n");
    printf("    Obj* v;\n");
    printf("    TarjanNode* node;\n");
    printf("    TarjanState state;\n");
    printf("    int pushed_a;\n");
    printf("    int pushed_b;\n");
    printf("    struct TarjanWorkFrame* next;\n");
    printf("} TarjanWorkFrame;\n\n");

    printf("static int push_work_frame(TarjanWorkFrame** stack, Obj* v, TarjanState state) {\n");
    printf("    TarjanWorkFrame* f = malloc(sizeof(TarjanWorkFrame));\n");
    printf("    if (!f) { TARJAN_OOM = 1; return 0; }\n");
    printf("    f->v = v;\n");
    printf("    f->node = NULL;\n");
    printf("    f->state = state;\n");
    printf("    f->pushed_a = 0;\n");
    printf("    f->pushed_b = 0;\n");
    printf("    f->next = *stack;\n");
    printf("    *stack = f;\n");
    printf("    return 1;\n");
    printf("}\n\n");

    printf("static TarjanWorkFrame* pop_work_frame(TarjanWorkFrame** stack) {\n");
    printf("    if (!*stack) return NULL;\n");
    printf("    TarjanWorkFrame* f = *stack;\n");
    printf("    *stack = f->next;\n");
    printf("    return f;\n");
    printf("}\n\n");

    printf("void tarjan_strongconnect(Obj* root_obj, SCC** result) {\n");
    printf("    if (!root_obj) return;\n");
    printf("    TarjanWorkFrame* work_stack = NULL;\n");
    printf("    if (!push_work_frame(&work_stack, root_obj, TARJAN_INIT)) return;\n\n");

    printf("    while (work_stack) {\n");
    printf("        TarjanWorkFrame* frame = work_stack;\n");
    printf("        Obj* v = frame->v;\n\n");
    printf("        if (TARJAN_OOM) {\n");
    printf("            while (work_stack) { free(pop_work_frame(&work_stack)); }\n");
    printf("            return;\n");
    printf("        }\n\n");

    printf("        switch (frame->state) {\n");
    printf("        case TARJAN_INIT: {\n");
    printf("            TarjanNode* node = get_tarjan_node(v);\n");
    printf("            if (!node) { TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("            if (node->index >= 0) {\n");
    printf("                free(pop_work_frame(&work_stack));\n");
    printf("                break;\n");
    printf("            }\n");
    printf("            node->index = TARJAN_INDEX;\n");
    printf("            node->lowlink = TARJAN_INDEX;\n");
    printf("            TARJAN_INDEX++;\n");
    printf("            tarjan_stack_push(v);\n");
    printf("            if (TARJAN_OOM) { while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("            node->on_stack = 1;\n");
    printf("            frame->node = node;\n");
    printf("            frame->state = TARJAN_AFTER_A;\n\n");

    printf("            if (v->is_pair && v->a) {\n");
    printf("                TarjanNode* w = get_tarjan_node(v->a);\n");
    printf("                if (!w) { TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                if (w->index < 0) {\n");
    printf("                    frame->pushed_a = 1;\n");
    printf("                    if (!push_work_frame(&work_stack, v->a, TARJAN_INIT)) { while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                } else if (w->on_stack) {\n");
    printf("                    if (node->lowlink > w->index) node->lowlink = w->index;\n");
    printf("                }\n");
    printf("            }\n");
    printf("            break;\n");
    printf("        }\n\n");

    printf("        case TARJAN_AFTER_A: {\n");
    printf("            TarjanNode* node = frame->node;\n");
    printf("            if (frame->pushed_a && v->is_pair && v->a) {\n");
    printf("                TarjanNode* w = get_tarjan_node(v->a);\n");
    printf("                if (!w) { TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                if (node->lowlink > w->lowlink) node->lowlink = w->lowlink;\n");
    printf("            }\n");
    printf("            frame->state = TARJAN_AFTER_B;\n\n");

    printf("            if (v->is_pair && v->b) {\n");
    printf("                TarjanNode* w = get_tarjan_node(v->b);\n");
    printf("                if (!w) { TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                if (w->index < 0) {\n");
    printf("                    frame->pushed_b = 1;\n");
    printf("                    if (!push_work_frame(&work_stack, v->b, TARJAN_INIT)) { while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                } else if (w->on_stack) {\n");
    printf("                    if (node->lowlink > w->index) node->lowlink = w->index;\n");
    printf("                }\n");
    printf("            }\n");
    printf("            break;\n");
    printf("        }\n\n");

    printf("        case TARJAN_AFTER_B: {\n");
    printf("            TarjanNode* node = frame->node;\n");
    printf("            if (frame->pushed_b && v->is_pair && v->b) {\n");
    printf("                TarjanNode* w = get_tarjan_node(v->b);\n");
    printf("                if (!w) { TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                if (node->lowlink > w->lowlink) node->lowlink = w->lowlink;\n");
    printf("            }\n\n");

    printf("            if (node->lowlink == node->index) {\n");
    printf("                SCC* scc = malloc(sizeof(SCC));\n");
    printf("                if (!scc) { TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                scc->id = SCC_NEXT_ID++;\n");
    printf("                scc->members = malloc(16 * sizeof(Obj*));\n");
    printf("                if (!scc->members) { free(scc); TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                scc->member_count = 0;\n");
    printf("                scc->capacity = 16;\n");
    printf("                scc->ref_count = 1;\n");
    printf("                scc->next = NULL;\n");
    printf("                scc->result_next = *result;\n");
    printf("                *result = scc;\n\n");

    printf("                Obj* w;\n");
    printf("                do {\n");
    printf("                    w = tarjan_stack_pop();\n");
    printf("                    if (!w) break;\n");
    printf("                    TarjanNode* w_node = get_tarjan_node(w);\n");
    printf("                    if (!w_node) break;\n");
    printf("                    w_node->on_stack = 0;\n");
    printf("                    w->scc_id = scc->id;\n");
    printf("                    if (scc->member_count >= scc->capacity) {\n");
    printf("                        if (scc->capacity > INT_MAX / 2) { free(scc->members); free(scc); TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                        scc->capacity *= 2;\n");
    printf("                        Obj** new_members = realloc(scc->members, scc->capacity * sizeof(Obj*));\n");
    printf("                        if (!new_members) { free(scc->members); free(scc); TARJAN_OOM = 1; while (work_stack) { free(pop_work_frame(&work_stack)); } return; }\n");
    printf("                        scc->members = new_members;\n");
    printf("                    }\n");
    printf("                    scc->members[scc->member_count++] = w;\n");
    printf("                } while (w != v);\n");
    printf("            }\n");
    printf("            free(pop_work_frame(&work_stack));\n");
    printf("            break;\n");
    printf("        }\n\n");
    
    printf("        case TARJAN_DONE:\n");
    printf("            free(pop_work_frame(&work_stack));\n");
    printf("            break;\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");

    printf("SCC* freeze_cyclic(Obj* root) {\n");
    printf("    // Reset Tarjan state\n");
    printf("    reset_tarjan_state();\n");
    printf("    \n");
    printf("    SCC* sccs = NULL;\n");
    printf("    tarjan_strongconnect(root, &sccs);\n");
    printf("    // Always clean up Tarjan state to prevent memory leak\n");
    printf("    reset_tarjan_state();\n");
    printf("    return sccs;\n");
    printf("}\n\n");

    printf("void release_scc(SCC* scc) {\n");
    printf("    if (!scc) return;\n");
    printf("    scc->ref_count--;\n");
    printf("    if (scc->ref_count == 0) {\n");
    printf("        for (int i = 0; i < scc->member_count; i++) {\n");
    printf("            invalidate_weak_refs_for(scc->members[i]);\n");
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
