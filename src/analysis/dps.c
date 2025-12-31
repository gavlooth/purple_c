// Phase 9: Destination-Passing Style (DPS) Implementation
// Source: "Destination-passing style for efficient memory management" (FHPC 2017)

#include "dps.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Analyze expression for DPS opportunities
DPSInfo* analyze_dps(Value* expr) {
    DPSInfo* info = malloc(sizeof(DPSInfo));
    if (!info) return NULL;
    info->dps_class = DPS_NONE;
    info->returns_fresh = 0;
    info->can_stack_dest = 0;
    info->alloc_count = 0;
    info->dest_type = NULL;

    if (!expr) return info;

    // Check for direct allocations (mk_int, mk_pair, cons)
    if (expr->tag == T_CELL) {
        Value* head = expr->cell.car;
        if (head && head->tag == T_SYM) {
            if (strcmp(head->s, "lift") == 0 ||
                strcmp(head->s, "cons") == 0) {
                info->returns_fresh = 1;
                info->alloc_count = 1;
                info->dest_type = "Obj";
            }
        }
    }

    return info;
}

// Check if a lambda is a DPS candidate
int is_dps_candidate(Value* lambda) {
    if (!lambda || lambda->tag != T_LAMBDA) return 0;

    // Analyze lambda body for fresh allocations in return position
    Value* body = lambda->lam.body;
    DPSInfo* info = analyze_dps(body);
    if (!info) return 0;

    int result = info->returns_fresh;
    free(info);

    return result;
}

// Find all DPS candidates in program
DPSCandidate* find_dps_candidates(Value* program) {
    (void)program;  // For now, return empty list
    // Full implementation would traverse program AST
    return NULL;
}

// Generate DPS runtime support
void gen_dps_runtime(void) {
    printf("\n// Phase 9: Destination-Passing Style (DPS) Runtime\n");
    printf("// Enables stack allocation of return values\n\n");

    // Destination type for pre-allocated slots
    printf("typedef struct Dest {\n");
    printf("    Obj* ptr;       // Pointer to destination memory\n");
    printf("    int is_stack;   // 1 if stack-allocated, 0 if heap\n");
    printf("} Dest;\n\n");

    // Create stack destination
    printf("// Allocate destination on stack\n");
    printf("#define STACK_DEST(name) \\\n");
    printf("    Obj name##_storage; \\\n");
    printf("    Dest name = { &name##_storage, 1 }\n\n");

    // Create heap destination
    printf("// Allocate destination on heap\n");
    printf("Dest heap_dest() {\n");
    printf("    Dest d;\n");
    printf("    d.ptr = malloc(sizeof(Obj));\n");
    printf("    if (!d.ptr) { d.is_stack = 0; return d; }  // Return with NULL ptr on OOM\n");
    printf("    d.is_stack = 0;\n");
    printf("    return d;\n");
    printf("}\n\n");

    // DPS-style integer constructor
    printf("// Write integer to destination\n");
    printf("Obj* write_int(Dest* dest, long value) {\n");
    printf("    if (!dest || !dest->ptr) return NULL;\n");
    printf("    dest->ptr->mark = 1;\n");
    printf("    dest->ptr->scc_id = -1;\n");
    printf("    dest->ptr->is_pair = 0;\n");
    printf("    dest->ptr->i = value;\n");
    printf("    return dest->ptr;\n");
    printf("}\n\n");

    // DPS-style pair constructor
    printf("// Write pair to destination\n");
    printf("Obj* write_pair(Dest* dest, Obj* a, Obj* b) {\n");
    printf("    if (!dest || !dest->ptr) return NULL;\n");
    printf("    dest->ptr->mark = 1;\n");
    printf("    dest->ptr->scc_id = -1;\n");
    printf("    dest->ptr->is_pair = 1;\n");
    printf("    dest->ptr->a = a;\n");
    printf("    dest->ptr->b = b;\n");
    printf("    return dest->ptr;\n");
    printf("}\n\n");

    // DPS-aware add function
    printf("// DPS arithmetic - write result to destination\n");
    printf("Obj* add_dps(Dest* dest, Obj* a, Obj* b) {\n");
    printf("    if (!a || !b) return write_int(dest, 0);\n");
    printf("    return write_int(dest, a->i + b->i);\n");
    printf("}\n\n");

    printf("Obj* sub_dps(Dest* dest, Obj* a, Obj* b) {\n");
    printf("    if (!a || !b) return write_int(dest, 0);\n");
    printf("    return write_int(dest, a->i - b->i);\n");
    printf("}\n\n");

    // Pipeline support - map with destination array
    printf("// DPS map - write results to destination array\n");
    printf("// Enables zero-allocation pipelines\n");
    printf("typedef Obj* (*MapFn)(Obj*);\n\n");

    printf("void map_dps(Dest* dests, MapFn f, Obj** inputs, int count) {\n");
    printf("    if (!dests || !f || !inputs) return;\n");
    printf("    for (int i = 0; i < count; i++) {\n");
    printf("        if (!dests[i].ptr) continue;\n");
    printf("        Obj* result = f(inputs[i]);\n");
    printf("        if (!result) continue;\n");
    printf("        dests[i].ptr->mark = result->mark;\n");
    printf("        dests[i].ptr->scc_id = result->scc_id;\n");
    printf("        dests[i].ptr->is_pair = result->is_pair;\n");
    printf("        if (result->is_pair) {\n");
    printf("            dests[i].ptr->a = result->a;\n");
    printf("            dests[i].ptr->b = result->b;\n");
    printf("        } else {\n");
    printf("            dests[i].ptr->i = result->i;\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");

    // Fold with destination
    printf("// DPS fold - accumulate into destination\n");
    printf("typedef Obj* (*FoldFn)(Obj*, Obj*);\n\n");

    printf("Obj* fold_dps(Dest* dest, FoldFn f, Obj* init, Obj** inputs, int count) {\n");
    printf("    if (!dest || !dest->ptr) return NULL;\n");
    printf("    Obj* acc = init;\n");
    printf("    for (int i = 0; i < count; i++) {\n");
    printf("        acc = f(acc, inputs[i]);\n");
    printf("    }\n");
    printf("    // Write final result to destination\n");
    printf("    if (!acc) return NULL;\n");
    printf("    dest->ptr->mark = acc->mark;\n");
    printf("    dest->ptr->scc_id = acc->scc_id;\n");
    printf("    dest->ptr->is_pair = acc->is_pair;\n");
    printf("    if (acc->is_pair) {\n");
    printf("        dest->ptr->a = acc->a;\n");
    printf("        dest->ptr->b = acc->b;\n");
    printf("    } else {\n");
    printf("        dest->ptr->i = acc->i;\n");
    printf("    }\n");
    printf("    return dest->ptr;\n");
    printf("}\n\n");
}

// Generate DPS-transformed function
void gen_dps_function(DPSCandidate* candidate, Value* body) {
    if (!candidate) return;

    printf("// DPS-transformed: %s\n", candidate->func_name);
    printf("Obj* %s_dps(Dest* dest", candidate->func_name);
    // Would add other parameters here
    printf(") {\n");

    // Generate body with DPS writes instead of allocations
    (void)body;  // Full implementation would transform body

    printf("    return dest->ptr;\n");
    printf("}\n\n");
}
