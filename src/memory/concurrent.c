// Phase 11: Concurrency Support Implementation
// Sources: SOTER (PLDI 2011), Concurrent Deferred RC (PLDI 2021), CIRC (PLDI 2024)

#include "concurrent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Analyze expression for ownership transfer
OwnershipClass analyze_ownership(Value* expr) {
    if (!expr) return OWN_LOCAL;

    // Check for channel operations
    if (expr->tag == T_CELL) {
        Value* head = expr->cell.car;
        if (head && head->tag == T_SYM) {
            if (strcmp(head->s, "send") == 0) {
                return OWN_TRANSFERRED;
            }
            if (strcmp(head->s, "spawn") == 0) {
                return OWN_SHARED;
            }
        }
    }

    return OWN_LOCAL;
}

// Detect thread spawn points
int is_spawn_point(Value* expr) {
    if (!expr || expr->tag != T_CELL) return 0;

    Value* head = expr->cell.car;
    if (head && head->tag == T_SYM) {
        return strcmp(head->s, "spawn") == 0 ||
               strcmp(head->s, "thread") == 0 ||
               strcmp(head->s, "parallel") == 0;
    }

    return 0;
}

// Generate concurrency runtime
void gen_concurrent_runtime(void) {
    printf("\n// Phase 11: Concurrency Support Runtime\n");
    printf("// Ownership transfer + atomic RC for zero-pause concurrent memory\n\n");

    printf("#include <stdatomic.h>\n");
    printf("#include <pthread.h>\n\n");

    // Thread-local storage
    printf("// Thread-local region for private allocations\n");
    printf("__thread int THREAD_ID = 0;\n");
    printf("__thread void* THREAD_REGION = NULL;\n");
    printf("__thread size_t THREAD_REGION_SIZE = 0;\n");
    printf("__thread size_t THREAD_REGION_USED = 0;\n\n");

    // Atomic reference count object
    printf("// Concurrent object with atomic reference count\n");
    printf("typedef struct ConcObj {\n");
    printf("    _Atomic int rc;           // Atomic reference count\n");
    printf("    int owner_thread;         // -1 if shared\n");
    printf("    int is_immutable;         // 1 if frozen (no sync needed)\n");
    printf("    int is_pair;\n");
    printf("    union {\n");
    printf("        long i;\n");
    printf("        struct { struct ConcObj *a, *b; };\n");
    printf("    };\n");
    printf("} ConcObj;\n\n");

    // Atomic increment
    printf("// Atomic increment (for shared objects)\n");
    printf("void conc_inc_ref(ConcObj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    atomic_fetch_add(&obj->rc, 1);\n");
    printf("}\n\n");

    // Atomic decrement with potential free
    printf("// Atomic decrement (may trigger deferred cleanup)\n");
    printf("void conc_dec_ref(ConcObj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    int old = atomic_fetch_sub(&obj->rc, 1);\n");
    printf("    if (old == 1) {\n");
    printf("        // Last reference - defer cleanup\n");
    printf("        // In real impl, add to thread-local deferred list\n");
    printf("        if (obj->is_pair) {\n");
    printf("            conc_dec_ref(obj->a);\n");
    printf("            conc_dec_ref(obj->b);\n");
    printf("        }\n");
    printf("        free(obj);\n");
    printf("    }\n");
    printf("}\n\n");

    // Concurrent allocator
    printf("// Allocate concurrent object\n");
    printf("ConcObj* conc_mk_int(long val) {\n");
    printf("    ConcObj* obj = malloc(sizeof(ConcObj));\n");
    printf("    if (!obj) return NULL;\n");
    printf("    atomic_init(&obj->rc, 1);\n");
    printf("    obj->owner_thread = THREAD_ID;\n");
    printf("    obj->is_immutable = 0;\n");
    printf("    obj->is_pair = 0;\n");
    printf("    obj->i = val;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("ConcObj* conc_mk_pair(ConcObj* a, ConcObj* b) {\n");
    printf("    ConcObj* obj = malloc(sizeof(ConcObj));\n");
    printf("    if (!obj) return NULL;\n");
    printf("    atomic_init(&obj->rc, 1);\n");
    printf("    obj->owner_thread = THREAD_ID;\n");
    printf("    obj->is_immutable = 0;\n");
    printf("    obj->is_pair = 1;\n");
    printf("    obj->a = a;\n");
    printf("    obj->b = b;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    // Channel for ownership transfer
    printf("// Channel for ownership transfer between threads\n");
    printf("typedef struct MsgChannel {\n");
    printf("    void** buffer;\n");
    printf("    int capacity;\n");
    printf("    _Atomic int head;\n");
    printf("    _Atomic int tail;\n");
    printf("    _Atomic int closed;\n");
    printf("    pthread_mutex_t mutex;\n");
    printf("    pthread_cond_t not_empty;\n");
    printf("    pthread_cond_t not_full;\n");
    printf("} MsgChannel;\n\n");

    // Create channel
    printf("// Create message channel\n");
    printf("MsgChannel* channel_create(int capacity) {\n");
    printf("    if (capacity <= 0) return NULL;  // Require positive capacity\n");
    printf("    MsgChannel* ch = malloc(sizeof(MsgChannel));\n");
    printf("    if (!ch) return NULL;\n");
    printf("    ch->buffer = malloc(capacity * sizeof(void*));\n");
    printf("    if (!ch->buffer) { free(ch); return NULL; }\n");
    printf("    ch->capacity = capacity;\n");
    printf("    atomic_init(&ch->head, 0);\n");
    printf("    atomic_init(&ch->tail, 0);\n");
    printf("    atomic_init(&ch->closed, 0);\n");
    printf("    pthread_mutex_init(&ch->mutex, NULL);\n");
    printf("    pthread_cond_init(&ch->not_empty, NULL);\n");
    printf("    pthread_cond_init(&ch->not_full, NULL);\n");
    printf("    return ch;\n");
    printf("}\n\n");

    // Send with ownership transfer
    printf("// Send message (transfers ownership, increments RC for safe sender cleanup)\n");
    printf("int channel_send(MsgChannel* ch, ConcObj* obj) {\n");
    printf("    if (!ch || !obj) return -1;\n");
    printf("    if (atomic_load(&ch->closed)) return -1;\n");
    printf("    pthread_mutex_lock(&ch->mutex);\n");
    printf("    int tail = atomic_load(&ch->tail);\n");
    printf("    int head = atomic_load(&ch->head);\n");
    printf("    while ((tail + 1) %% ch->capacity == head) {\n");
    printf("        pthread_cond_wait(&ch->not_full, &ch->mutex);\n");
    printf("        if (atomic_load(&ch->closed)) {\n");
    printf("            pthread_mutex_unlock(&ch->mutex);\n");
    printf("            return -1;\n");
    printf("        }\n");
    printf("        tail = atomic_load(&ch->tail);\n");
    printf("        head = atomic_load(&ch->head);\n");
    printf("    }\n");
    printf("    // Increment RC so sender can safely dec_ref after send\n");
    printf("    atomic_fetch_add(&obj->rc, 1);\n");
    printf("    obj->owner_thread = -1;  // Mark as in-transit\n");
    printf("    ch->buffer[tail] = obj;\n");
    printf("    atomic_store(&ch->tail, (tail + 1) %% ch->capacity);\n");
    printf("    pthread_cond_signal(&ch->not_empty);\n");
    printf("    pthread_mutex_unlock(&ch->mutex);\n");
    printf("    return 0;\n");
    printf("}\n\n");

    // Receive with ownership transfer
    printf("// Receive message (receives ownership)\n");
    printf("ConcObj* channel_recv(MsgChannel* ch) {\n");
    printf("    if (!ch) return NULL;\n");
    printf("    pthread_mutex_lock(&ch->mutex);\n");
    printf("    int head = atomic_load(&ch->head);\n");
    printf("    int tail = atomic_load(&ch->tail);\n");
    printf("    while (head == tail) {\n");
    printf("        if (atomic_load(&ch->closed)) {\n");
    printf("            pthread_mutex_unlock(&ch->mutex);\n");
    printf("            return NULL;\n");
    printf("        }\n");
    printf("        pthread_cond_wait(&ch->not_empty, &ch->mutex);\n");
    printf("        head = atomic_load(&ch->head);\n");
    printf("        tail = atomic_load(&ch->tail);\n");
    printf("    }\n");
    printf("    ConcObj* obj = ch->buffer[head];\n");
    printf("    // Take ownership: receiver becomes owner\n");
    printf("    obj->owner_thread = THREAD_ID;\n");
    printf("    atomic_store(&ch->head, (head + 1) %% ch->capacity);\n");
    printf("    pthread_cond_signal(&ch->not_full);\n");
    printf("    pthread_mutex_unlock(&ch->mutex);\n");
    printf("    return obj;\n");
    printf("}\n\n");

    // Close channel
    printf("// Close channel\n");
    printf("void channel_close(MsgChannel* ch) {\n");
    printf("    if (!ch) return;\n");
    printf("    atomic_store(&ch->closed, 1);\n");
    printf("    pthread_cond_broadcast(&ch->not_empty);\n");
    printf("    pthread_cond_broadcast(&ch->not_full);\n");
    printf("}\n\n");

    // Destroy channel
    printf("// Destroy channel\n");
    printf("void channel_destroy(MsgChannel* ch) {\n");
    printf("    if (!ch) return;\n");
    printf("    pthread_mutex_destroy(&ch->mutex);\n");
    printf("    pthread_cond_destroy(&ch->not_empty);\n");
    printf("    pthread_cond_destroy(&ch->not_full);\n");
    printf("    free(ch->buffer);\n");
    printf("    free(ch);\n");
    printf("}\n\n");

    // Thread spawn helper
    printf("// Thread spawn with ownership semantics\n");
    printf("typedef struct SpawnArgs {\n");
    printf("    void* (*fn)(void*);\n");
    printf("    void* arg;\n");
    printf("    int thread_id;\n");
    printf("} SpawnArgs;\n\n");

    printf("static int next_thread_id = 1;\n\n");

    printf("void* thread_wrapper(void* args) {\n");
    printf("    SpawnArgs* sa = (SpawnArgs*)args;\n");
    printf("    THREAD_ID = sa->thread_id;\n");
    printf("    void* result = sa->fn(sa->arg);\n");
    printf("    free(sa);\n");
    printf("    return result;\n");
    printf("}\n\n");

    printf("pthread_t spawn_thread(void* (*fn)(void*), void* arg) {\n");
    printf("    SpawnArgs* sa = malloc(sizeof(SpawnArgs));\n");
    printf("    if (!sa) return (pthread_t)0;\n");
    printf("    sa->fn = fn;\n");
    printf("    sa->arg = arg;\n");
    printf("    sa->thread_id = next_thread_id++;\n");
    printf("    pthread_t tid;\n");
    printf("    if (pthread_create(&tid, NULL, thread_wrapper, sa) != 0) {\n");
    printf("        free(sa);\n");
    printf("        return (pthread_t)0;\n");
    printf("    }\n");
    printf("    return tid;\n");
    printf("}\n\n");

    // Freeze for immutable sharing
    printf("// Freeze object for immutable sharing (no sync needed)\n");
    printf("void conc_freeze(ConcObj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    if (obj->is_immutable) return;\n");
    printf("    obj->is_immutable = 1;\n");
    printf("    obj->owner_thread = -1;  // Shared\n");
    printf("    if (obj->is_pair) {\n");
    printf("        conc_freeze(obj->a);\n");
    printf("        conc_freeze(obj->b);\n");
    printf("    }\n");
    printf("}\n\n");
}

// Generate atomic RC operations
void gen_atomic_rc_ops(void) {
    // Already included in gen_concurrent_runtime
}
