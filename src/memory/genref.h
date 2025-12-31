/*
 * Random Generational References - Vale-style use-after-free detection
 *
 * Each object has a random 64-bit generation number.
 * Each pointer remembers the generation it was created with.
 * On dereference: if ptr.gen != obj.gen → use-after-free detected
 * On free: obj.gen = 0 (invalidates all existing pointers)
 *
 * Random vs Sequential:
 * - Sequential: obj.gen++ on each reuse (requires tracking, overflow handling)
 * - Random: obj.gen = random64() on create, obj.gen = 0 on free (simpler, faster)
 *
 * Collision probability: 1/2^64 per check (negligible)
 */

#ifndef GENREF_H
#define GENREF_H

#include <stdint.h>
#include <stdbool.h>

/* Generation type - 64-bit random number */
typedef uint64_t Generation;

/* Forward declarations */
typedef struct GenObj GenObj;
typedef struct GenRef GenRef;
typedef struct GenClosure GenClosure;
typedef struct GenRefContext GenRefContext;

/* GenObj - an object with generational safety */
struct GenObj {
    Generation generation;
    void* data;
    void (*destructor)(void*);
    bool freed;
};

/* GenRef - a generational reference (fat pointer) */
struct GenRef {
    GenObj* target;
    Generation remembered_gen;
    const char* source_desc;  /* For debugging */
};

/* GenClosure - closure with tracked captures */
struct GenClosure {
    GenRef** captures;
    int capture_count;
    void* (*fn)(void* context);
    void* context;
};

/* GenRefContext - manages generational objects */
struct GenRefContext {
    GenObj** objects;
    int object_count;
    int object_capacity;
};

/* Statistics */
typedef struct {
    int64_t total_allocations;
    int64_t total_frees;
    int64_t total_derefs;
    int64_t uaf_detected;
} GenRefStats;

/* Error codes */
typedef enum {
    GENREF_OK = 0,
    GENREF_ERR_NULL = -1,
    GENREF_ERR_FREED = -2,
    GENREF_ERR_UAF = -3,
    GENREF_ERR_ALLOC_FAILED = -4,
    GENREF_ERR_INVALID_CAPTURE = -5
} GenRefError;

/* Context management */
GenRefContext* genref_context_new(void);
void genref_context_free(GenRefContext* ctx);

/* Object operations */
GenObj* genref_alloc(GenRefContext* ctx, void* data, void (*destructor)(void*));
void genref_free(GenObj* obj);

/* Reference operations */
GenRef* genref_create_ref(GenObj* obj, const char* source_desc);
void* genref_deref(GenRef* ref, GenRefError* err);
bool genref_is_valid(GenRef* ref);
void genref_ref_free(GenRef* ref);

/* Closure operations */
GenClosure* genref_closure_new(GenRef** captures, int count, void* (*fn)(void*), void* context);
void* genref_closure_call(GenClosure* closure, GenRefError* err);
GenRefError genref_closure_validate(GenClosure* closure);
void genref_closure_free(GenClosure* closure);

/* Random generation */
Generation genref_random_generation(void);

#endif /* GENREF_H */
