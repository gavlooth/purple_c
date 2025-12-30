#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>
#include "src/memory/arena.h"

static int had_failure = 0;
static int arena_release_count = 0;

// ==============================================================================
// MOCK RUNTIME (Copied/Adapted from what gen_concurrent_runtime emits)
// ==============================================================================

// Thread-local variables
__thread int THREAD_ID = 0;

// Concurrent object with atomic reference count
typedef struct ConcObj {
    _Atomic int rc;           // Atomic reference count
    int owner_thread;         // -1 if shared
    int is_immutable;         // 1 if frozen (no sync needed)
    int is_pair;
    union {
        long i;
        struct { struct ConcObj *a, *b; };
    };
} ConcObj;

// Atomic decrement with potential free
void conc_dec_ref(ConcObj* obj) {
    if (!obj) return;
    if (obj->is_immutable) return; 
    int old = atomic_fetch_sub(&obj->rc, 1);
    if (old == 1) {
        printf("[MockRuntime] Freeing object %p (Value: %ld)\n", (void*)obj, obj->i);
        free(obj);
    }
}

// Allocate concurrent object
ConcObj* conc_mk_int(long val) {
    ConcObj* obj = malloc(sizeof(ConcObj));
    atomic_init(&obj->rc, 1);
    obj->owner_thread = THREAD_ID;
    obj->is_immutable = 0;
    obj->is_pair = 0;
    obj->i = val;
    return obj;
}

// Channel for ownership transfer
typedef struct MsgChannel {
    void** buffer;
    int capacity;
    _Atomic int head;
    _Atomic int tail;
    _Atomic int closed;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} MsgChannel;

// Create channel
MsgChannel* channel_create(int capacity) {
    MsgChannel* ch = malloc(sizeof(MsgChannel));
    ch->buffer = malloc(capacity * sizeof(void*));
    ch->capacity = capacity;
    atomic_init(&ch->head, 0);
    atomic_init(&ch->tail, 0);
    atomic_init(&ch->closed, 0);
    pthread_mutex_init(&ch->mutex, NULL);
    pthread_cond_init(&ch->not_empty, NULL);
    pthread_cond_init(&ch->not_full, NULL);
    return ch;
}

// Send with ownership transfer
int channel_send(MsgChannel* ch, ConcObj* obj) {
    if (atomic_load(&ch->closed)) return -1;
    pthread_mutex_lock(&ch->mutex);
    int tail = atomic_load(&ch->tail);
    int head = atomic_load(&ch->head);
    while ((tail + 1) % ch->capacity == head) {
        pthread_cond_wait(&ch->not_full, &ch->mutex);
        if (atomic_load(&ch->closed)) {
            pthread_mutex_unlock(&ch->mutex);
            return -1;
        }
    }
    // Transfer ownership: sender gives up reference
    obj->owner_thread = -1;  // Mark as in-transit
    ch->buffer[tail] = obj;
    atomic_store(&ch->tail, (tail + 1) % ch->capacity);
    pthread_cond_signal(&ch->not_empty);
    pthread_mutex_unlock(&ch->mutex);
    return 0;
}

// Receive with ownership transfer
ConcObj* channel_recv(MsgChannel* ch) {
    pthread_mutex_lock(&ch->mutex);
    int head = atomic_load(&ch->head);
    int tail = atomic_load(&ch->tail);
    while (head == tail) {
        if (atomic_load(&ch->closed)) {
            pthread_mutex_unlock(&ch->mutex);
            return NULL;
        }
        pthread_cond_wait(&ch->not_empty, &ch->mutex);
    }
    ConcObj* obj = (ConcObj*)ch->buffer[head];
    // Take ownership: receiver becomes owner
    obj->owner_thread = THREAD_ID;
    atomic_store(&ch->head, (head + 1) % ch->capacity);
    pthread_cond_signal(&ch->not_full);
    pthread_mutex_unlock(&ch->mutex);
    return obj;
}

void channel_destroy(MsgChannel* ch) {
    pthread_mutex_destroy(&ch->mutex);
    pthread_cond_destroy(&ch->not_empty);
    pthread_cond_destroy(&ch->not_full);
    free(ch->buffer);
    free(ch);
}

// ---------------------------------------------------
// Issue 1: Concurrency Use-After-Free
// ---------------------------------------------------
void test_concurrency_race() {
    printf("\n=== Test: Concurrency Use-After-Free ===\n");
    MsgChannel* ch = channel_create(10);
    ConcObj* shared_obj = conc_mk_int(42);
    channel_send(ch, shared_obj); 
    conc_dec_ref(shared_obj); 
    ConcObj* received = channel_recv(ch);
    if (received) {
        printf("[Receiver] Got object at %p. Value: %ld\n", (void*)received, received->i);
    }
    channel_destroy(ch);
}

typedef struct MockHeapObj {
    int ref_count;
    int value;
} MockHeapObj;

static void arena_release_mock(void* ptr) {
    MockHeapObj* obj = (MockHeapObj*)ptr;
    if (!obj) return;
    obj->ref_count--;
    arena_release_count++;
}

void test_arena_leak() {
    printf("\n=== Test: Arena Memory Leak ===\n");
    Arena* a = arena_create(4096);
    MockHeapObj* heap_obj = malloc(sizeof(MockHeapObj));
    heap_obj->ref_count = 1;
    heap_obj->value = 999;
    MockHeapObj** arena_ptr = arena_alloc(a, sizeof(MockHeapObj*));
    *arena_ptr = heap_obj;
    arena_register_external(a, heap_obj, arena_release_mock);
    arena_destroy(a);
    printf("[ArenaTest] Arena destroyed. Heap object RefCount: %d\n", heap_obj->ref_count);
    if (arena_release_count != 1 || heap_obj->ref_count > 0) {
        printf("FAIL: Heap object leaked!\n");
        had_failure = 1;
    }
    free(heap_obj);
}

// ---------------------------------------------------
// Issue 4: Weak Reference Dangling
// ---------------------------------------------------
typedef struct WeakRef {
    void* target;
    int alive;
} WeakRef;

typedef struct ZombieObj {
    int _rc;
    int _weak_rc;
    ConcObj* child; // Strong reference to a child
} ZombieObj;

void test_weak_ref_dangling() {
    printf("\n=== Test: Weak Reference Dangling ===\n");
    
    // 1. Create target object and child
    ZombieObj* target = malloc(sizeof(ZombieObj));
    target->_rc = 1;
    target->_weak_rc = 1; // Simulated weak ref exists
    target->child = conc_mk_int(777);
    
    // 2. Create weak reference
    WeakRef* w = malloc(sizeof(WeakRef));
    w->target = target;
    w->alive = 1;
    
    // 3. Release target (The Bug: doesn't invalidate weak ref)
    printf("[WeakTest] Releasing target object...\n");
    target->_rc--;
    if (target->_rc == 0) {
        printf("[WeakTest] Target became zombie. Releasing child %p...\n", (void*)target->child);
        conc_dec_ref(target->child);
        target->child = NULL; // It's a zombie now
        if (target->_weak_rc == 0) free(target);
        else target->_rc = -1; 
    }
    
    // 4. Dereference weak ref (The Bug: returns target despite it being a zombie)
    printf("[WeakTest] Dereferencing weak ref (alive: %d)...\n", w->alive);
    ZombieObj* derefed = (w->alive) ? (ZombieObj*)w->target : NULL;
    
    if (derefed && derefed->_rc == -1) {
        printf("[WeakTest] FAIL: Got zombie object! Accessing child: %p\n", (void*)derefed->child);
        // Accessing derefed->child is now dangerous/invalid
        had_failure = 1;
    }
    
    free(w);
    free(target);
}

// ---------------------------------------------------
// Issue 5: Reuse Optimization Leak
// ---------------------------------------------------
void test_reuse_leak() {
    printf("\n=== Test: Reuse Optimization Leak ===\n");
    
    // 1. Create a pair (ref count 1)
    ConcObj* child_a = conc_mk_int(100);
    ConcObj* child_b = conc_mk_int(200);
    ConcObj* pair = malloc(sizeof(ConcObj));
    pair->rc = 1;
    pair->is_pair = 1;
    pair->a = child_a;
    pair->b = child_b;
    
    printf("[ReuseTest] Pair %p holds children %p, %p\n", (void*)pair, (void*)child_a, (void*)child_b);
    
    // 2. Reuse as INT (The Bug: doesn't dec_ref children)
    printf("[ReuseTest] Reusing pair as INT...\n");
    if (pair->rc == 1) { // try_reuse logic
        pair->is_pair = 0;
        pair->i = 300;
        // BUG: child_a and child_b ref counts were NOT decremented
    }
    
    printf("[ReuseTest] Child A ref count: %d (Expected: 0 -> Freed)\n", atomic_load(&child_a->rc));
    if (atomic_load(&child_a->rc) > 0) {
        printf("FAIL: Child object leaked during reuse!\n");
        had_failure = 1;
    }
    
    // Cleanup
    conc_dec_ref(child_a);
    conc_dec_ref(child_b);
    free(pair);
}

// ---------------------------------------------------
// Issue 3: Shape Analysis (Recursion)
// ---------------------------------------------------
typedef struct CyclicNode {
    struct CyclicNode* next;
    char padding[1024];
} CyclicNode;

void free_tree_simulated(CyclicNode* node, int depth) {
    if (!node) return;
    if (depth > 1000) { 
         printf("FAIL: Stack overflow imminent (depth %d)!\n", depth);
         exit(1);
    }
    free_tree_simulated(node->next, depth + 1);
    free(node);
}

void test_shape_analysis_unsafe() {
    printf("\n=== Test: Unsafe Shape Analysis ===\n");
    CyclicNode* n1 = malloc(sizeof(CyclicNode));
    CyclicNode* n2 = malloc(sizeof(CyclicNode));
    n1->next = n2;
    n2->next = n1;
    free_tree_simulated(n1, 0);
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (strcmp(argv[1], "concurrency") == 0) test_concurrency_race();
        else if (strcmp(argv[1], "arena") == 0) test_arena_leak();
        else if (strcmp(argv[1], "shape") == 0) test_shape_analysis_unsafe();
        else if (strcmp(argv[1], "weakref") == 0) test_weak_ref_dangling();
        else if (strcmp(argv[1], "reuse") == 0) test_reuse_leak();
    } else {
        test_concurrency_race();
        test_arena_leak();
        test_weak_ref_dangling();
        test_reuse_leak();
        test_shape_analysis_unsafe();
    }
    return had_failure ? 1 : 0;
}
