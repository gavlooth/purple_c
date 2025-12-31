/*
 * Constraint References - Assertion-based safety for complex patterns
 *
 * For graphs, observers, callbacks, and other complex ownership patterns.
 *
 * Key concept:
 * - Each object has ONE owner (responsible for freeing)
 * - Multiple non-owning "constraint" references can exist
 * - On free: ASSERT that constraint count is zero
 * - If assertion fails: dangling references would have been created
 *
 * This is primarily a DEBUG/DEVELOPMENT tool - catches errors at free time
 * rather than at dereference time.
 */

#ifndef CONSTRAINT_H
#define CONSTRAINT_H

#include <stdint.h>
#include <stdbool.h>

/* Forward declarations */
typedef struct ConstraintObj ConstraintObj;
typedef struct ConstraintRef ConstraintRef;
typedef struct ConstraintContext ConstraintContext;

/* Maximum constraint sources to track (for debugging) */
#define MAX_CONSTRAINT_SOURCES 16

/* ConstraintObj - an object with constraint reference tracking */
struct ConstraintObj {
    void* data;
    void (*destructor)(void*);
    const char* owner;
    int32_t constraint_count;
    bool freed;
    const char* constraint_sources[MAX_CONSTRAINT_SOURCES];
    int source_count;
};

/* ConstraintRef - non-owning reference that constrains object lifetime */
struct ConstraintRef {
    ConstraintObj* target;
    const char* source;
    bool released;
};

/* ConstraintContext - manages constraint objects */
struct ConstraintContext {
    ConstraintObj** objects;
    int object_count;
    int object_capacity;
    bool assert_on_error;
    char** violations;
    int violation_count;
    int violation_capacity;
};

/* Statistics */
typedef struct {
    int total_objects;
    int active_objects;
    int total_constraints;
    int violation_count;
} ConstraintStats;

/* Error codes */
typedef enum {
    CONSTRAINT_OK = 0,
    CONSTRAINT_ERR_NULL = -1,
    CONSTRAINT_ERR_FREED = -2,
    CONSTRAINT_ERR_VIOLATION = -3,
    CONSTRAINT_ERR_DOUBLE_FREE = -4,
    CONSTRAINT_ERR_DOUBLE_RELEASE = -5,
    CONSTRAINT_ERR_RELEASED = -6,
    CONSTRAINT_ERR_ALLOC_FAILED = -7
} ConstraintError;

/* Context management */
ConstraintContext* constraint_context_new(bool assert_on_error);
void constraint_context_free(ConstraintContext* ctx);

/* Object operations */
ConstraintObj* constraint_alloc(ConstraintContext* ctx, void* data,
                                void (*destructor)(void*), const char* owner);
ConstraintError constraint_free(ConstraintContext* ctx, ConstraintObj* obj);

/* Reference operations */
ConstraintRef* constraint_add(ConstraintObj* obj, const char* source);
ConstraintError constraint_release(ConstraintRef* ref);
void* constraint_deref(ConstraintRef* ref, ConstraintError* err);
bool constraint_ref_is_valid(ConstraintRef* ref);
void constraint_ref_free(ConstraintRef* ref);

/* Violation tracking */
bool constraint_has_violations(ConstraintContext* ctx);
int constraint_get_violation_count(ConstraintContext* ctx);
const char* constraint_get_violation(ConstraintContext* ctx, int index);
void constraint_clear_violations(ConstraintContext* ctx);

/* Statistics */
ConstraintStats constraint_get_stats(ConstraintContext* ctx);

#endif /* CONSTRAINT_H */
