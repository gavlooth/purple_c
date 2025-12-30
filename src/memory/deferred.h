#ifndef PURPLE_DEFERRED_H
#define PURPLE_DEFERRED_H

#include "../types.h"
#include "../util/hashmap.h"

// -- Phase 7: Deferred RC Fallback --
// For mutable cyclic structures that never freeze
// Bounded O(k) processing at safe points; no stop-the-world.

// Deferred decrement entry
typedef struct DeferredDec {
    void* obj;
    int count;  // Number of pending decrements
    struct DeferredDec* next;
} DeferredDec;

// Deferred processing context
typedef struct DeferredContext {
    DeferredDec* pending;
    HashMap* obj_lookup;  // O(1) lookup: obj -> DeferredDec*
    int pending_count;
    int batch_size;       // Max decrements per safe point
    int total_deferred;   // Statistics
    int dropped_decrements; // Count of decrements lost to OOM
} DeferredContext;

// Context management
DeferredContext* mk_deferred_context(int batch_size);
void free_deferred_context(DeferredContext* ctx);

// Deferred operations
void defer_decrement(DeferredContext* ctx, void* obj);
void process_deferred(DeferredContext* ctx, int max_count);
void flush_deferred(DeferredContext* ctx);

// Safe point insertion
int should_process_deferred(DeferredContext* ctx);

// Code generation
void gen_deferred_runtime(void);
void gen_safe_point(const char* location);

#endif // PURPLE_DEFERRED_H
