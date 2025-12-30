#ifndef PURPLE_SCC_H
#define PURPLE_SCC_H

#include "../types.h"

// -- Phase 6b: SCC-based RC (ISMM 2024) --
// Reference Counting Deeply Immutable Data Structures with Cycles
// For frozen (immutable) cyclic structures

// Runtime object with SCC support
typedef struct Obj {
    int mark;           // Reference count / mark bit
    int scc_id;         // SCC identifier (-1 if not in SCC)
    union {
        long i;
        struct { struct Obj *a, *b; };
    };
} Obj;

// SCC Node for Tarjan's algorithm
typedef struct SCCNode {
    int id;
    int lowlink;
    int on_stack;
    Obj* obj;
    struct SCCNode* next;
} SCCNode;

// Strongly Connected Component
typedef struct SCC {
    int id;
    Obj** members;
    int member_count;
    int capacity;
    int ref_count;      // Single RC for entire SCC
    int frozen;         // 1 if frozen (immutable)
    struct SCC* next;
} SCC;

// SCC Registry
typedef struct SCCRegistry {
    SCC* sccs;
    int next_id;
    SCCNode* node_map;  // For Tarjan's algorithm
    SCCNode* stack;
    int index;
} SCCRegistry;

// Freeze point detection
typedef struct FreezePoint {
    int line_number;
    char* var_name;
    Value* expr;
    int is_cyclic;
    struct FreezePoint* next;
} FreezePoint;

// SCC Registry management
SCCRegistry* mk_scc_registry(void);
void free_scc_registry(SCCRegistry* reg);

// Tarjan's SCC algorithm
SCC* compute_sccs(SCCRegistry* reg, Obj* root);
void tarjan_dfs(SCCRegistry* reg, Obj* v, SCC** result);

// SCC operations
SCC* create_scc(SCCRegistry* reg);
void add_to_scc(SCC* scc, Obj* obj);
SCC* find_scc(SCCRegistry* reg, int scc_id);

// Freeze operations
Obj* freeze_cyclic(Obj* root);
void release_scc(SCC* scc);
void inc_scc_ref(SCC* scc);

// Freeze point detection
FreezePoint* detect_freeze_points(Value* expr);
int is_frozen_after_construction(const char* var, Value* body);

// Code generation
void gen_scc_runtime(void);
void gen_freeze_call(const char* var);
void gen_release_scc_call(const char* var);

#endif // PURPLE_SCC_H
