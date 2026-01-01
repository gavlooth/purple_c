// Phase 11: Concurrency Support
// Ownership transfer semantics + atomic RC for shared data
// Constraint: no stop-the-world; synchronization is local to shared objects/channels.
// Sources: SOTER (PLDI 2011), Concurrent Deferred RC (PLDI 2021)

#ifndef CONCURRENT_H
#define CONCURRENT_H

#include "../types.h"

// Ownership classification for concurrent access
typedef enum {
    OWN_LOCAL,          // Thread-local, pure ASAP
    OWN_TRANSFERRED,    // Ownership transferred via channel
    OWN_SHARED,         // Shared between threads, needs atomic RC
    OWN_IMMUTABLE       // Immutable shared data, no sync needed
} OwnershipClass;

// Thread-local region for private allocations
typedef struct ThreadRegion {
    int thread_id;
    void* region_start;
    size_t region_size;
    size_t used;
    struct ThreadRegion* next;
} ThreadRegion;

// ConcurrentChannel for ownership transfer (renamed to avoid conflict with CSP Channel)
typedef struct ConcurrentChannel {
    int id;
    void** buffer;          // Message buffer
    int capacity;
    int head;
    int tail;
    int closed;
    // Synchronization primitives (pthread in runtime)
} ConcurrentChannel;

// Message with ownership metadata
typedef struct Message {
    void* data;
    OwnershipClass ownership;
    int source_thread;
} Message;

// Concurrent object header (for shared objects)
typedef struct ConcurrentObj {
    int atomic_rc;          // Atomic reference count
    int thread_owner;       // Owning thread (-1 if shared)
    OwnershipClass ownership;
} ConcurrentObj;

// Analyze expression for ownership transfer points
OwnershipClass analyze_ownership(Value* expr);

// Detect thread spawn points
int is_spawn_point(Value* expr);

// Generate concurrency runtime
void gen_concurrent_runtime(void);

// Generate atomic RC operations
void gen_atomic_rc_ops(void);

#endif // CONCURRENT_H
