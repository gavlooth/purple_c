// Phase 9: Destination-Passing Style (DPS) Analysis
// Transforms functions returning fresh allocations to take destination pointers
// Source: "Destination-passing style for efficient memory management" (FHPC 2017)

#ifndef DPS_H
#define DPS_H

#include "../types.h"

// DPS candidate classification
typedef enum {
    DPS_NONE,           // Not a DPS candidate
    DPS_STACK,          // Can stack-allocate destination
    DPS_CALLER_OWNED,   // Caller provides destination
    DPS_PIPELINE        // Part of allocation pipeline (map, fold, etc.)
} DPSClass;

// DPS analysis result for a function/expression
typedef struct DPSInfo {
    DPSClass dps_class;
    int returns_fresh;      // 1 if function returns freshly allocated value
    int can_stack_dest;     // 1 if destination can be stack-allocated
    int alloc_count;        // Number of allocations in function body
    char* dest_type;        // Type of destination (e.g., "Obj")
} DPSInfo;

// DPS candidate site
typedef struct DPSCandidate {
    char* func_name;        // Function name
    DPSClass dps_class;
    int stack_eligible;     // 1 if can use stack destination
    struct DPSCandidate* next;
} DPSCandidate;

// Analyze expression for DPS opportunities
DPSInfo* analyze_dps(Value* expr);

// Check if a function is a DPS candidate
int is_dps_candidate(Value* lambda);

// Find all DPS candidates in program
DPSCandidate* find_dps_candidates(Value* program);

// Generate DPS runtime support
void gen_dps_runtime(void);

// Generate DPS-transformed function
void gen_dps_function(DPSCandidate* candidate, Value* body);

#endif // DPS_H
