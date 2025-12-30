// Phase 10: Exception Handling with ASAP Cleanup
// Generates landing pads for stack unwinding
// Source: LLVM Exception Handling model

#ifndef EXCEPTION_H
#define EXCEPTION_H

#include "../types.h"

// Live allocation tracking for cleanup
typedef struct LiveAlloc {
    char* var_name;           // Variable name
    char* type_name;          // Type for cleanup selection
    int program_point;        // Where allocated
    struct LiveAlloc* next;
} LiveAlloc;

// Cleanup action for landing pad
typedef struct CleanupAction {
    char* var_name;           // Variable to clean up
    char* cleanup_fn;         // Function to call (free_tree, dec_ref, etc.)
    struct CleanupAction* next;
} CleanupAction;

// Landing pad for exception handling
typedef struct LandingPad {
    int id;                   // Unique ID
    int try_start;            // Start of protected region
    int try_end;              // End of protected region
    CleanupAction* cleanups;  // Actions to perform
    struct LandingPad* next;
} LandingPad;

// Exception context for nested try/catch
typedef struct ExceptionContext {
    int depth;                // Nesting depth
    LandingPad* current_pad;  // Current landing pad
    LiveAlloc* live_allocs;   // Live allocations at this point
    struct ExceptionContext* parent;
} ExceptionContext;

// Track a new allocation
void track_alloc(ExceptionContext* ctx, const char* var, const char* type);

// Remove allocation from tracking (after free)
void untrack_alloc(ExceptionContext* ctx, const char* var);

// Get live allocations at current point
LiveAlloc* get_live_allocs(ExceptionContext* ctx);

// Create landing pad for current context
LandingPad* create_landing_pad(ExceptionContext* ctx);

// Generate exception runtime
void gen_exception_runtime(void);

// Generate cleanup code for landing pad
void gen_landing_pad_code(LandingPad* pad);

#endif // EXCEPTION_H
