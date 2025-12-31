/*
 * Constraint References implementation
 * Assertion-based safety for complex patterns
 */

#include "constraint.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Helper: grow array */
static void* grow_array(void* arr, int* capacity, size_t elem_size) {
    int new_cap = (*capacity == 0) ? 8 : (*capacity * 2);
    void* new_arr = realloc(arr, new_cap * elem_size);
    if (new_arr) {
        *capacity = new_cap;
    }
    return new_arr;
}

/* Helper: add violation to context */
static void add_violation(ConstraintContext* ctx, const char* message) {
    if (!ctx) return;

    /* Duplicate the message */
    char* msg_copy = strdup(message);
    if (!msg_copy) return;

    if (ctx->violation_count >= ctx->violation_capacity) {
        char** new_violations = grow_array(
            ctx->violations,
            &ctx->violation_capacity,
            sizeof(char*)
        );
        if (!new_violations) {
            free(msg_copy);
            return;
        }
        ctx->violations = new_violations;
    }
    ctx->violations[ctx->violation_count++] = msg_copy;
}

/* Create new context */
ConstraintContext* constraint_context_new(bool assert_on_error) {
    ConstraintContext* ctx = calloc(1, sizeof(ConstraintContext));
    if (!ctx) return NULL;

    ctx->objects = NULL;
    ctx->object_count = 0;
    ctx->object_capacity = 0;
    ctx->assert_on_error = assert_on_error;
    ctx->violations = NULL;
    ctx->violation_count = 0;
    ctx->violation_capacity = 0;

    return ctx;
}

/* Free context */
void constraint_context_free(ConstraintContext* ctx) {
    if (!ctx) return;

    /* Free violation strings */
    for (int i = 0; i < ctx->violation_count; i++) {
        free(ctx->violations[i]);
    }
    free(ctx->violations);
    free(ctx->objects);
    free(ctx);
}

/* Allocate new constraint object */
ConstraintObj* constraint_alloc(ConstraintContext* ctx, void* data,
                                void (*destructor)(void*), const char* owner) {
    ConstraintObj* obj = calloc(1, sizeof(ConstraintObj));
    if (!obj) return NULL;

    obj->data = data;
    obj->destructor = destructor;
    obj->owner = owner;
    obj->constraint_count = 0;
    obj->freed = false;
    obj->source_count = 0;

    /* Add to context if provided */
    if (ctx) {
        if (ctx->object_count >= ctx->object_capacity) {
            ConstraintObj** new_objs = grow_array(
                ctx->objects,
                &ctx->object_capacity,
                sizeof(ConstraintObj*)
            );
            if (!new_objs) {
                free(obj);
                return NULL;
            }
            ctx->objects = new_objs;
        }
        ctx->objects[ctx->object_count++] = obj;
    }

    return obj;
}

/* Add constraint reference to object */
ConstraintRef* constraint_add(ConstraintObj* obj, const char* source) {
    if (!obj || obj->freed) return NULL;

    ConstraintRef* ref = calloc(1, sizeof(ConstraintRef));
    if (!ref) return NULL;

    ref->target = obj;
    ref->source = source;
    ref->released = false;

    obj->constraint_count++;

    /* Track source for debugging */
    if (obj->source_count < MAX_CONSTRAINT_SOURCES) {
        obj->constraint_sources[obj->source_count++] = source;
    }

    return ref;
}

/* Release constraint reference */
ConstraintError constraint_release(ConstraintRef* ref) {
    if (!ref) return CONSTRAINT_ERR_NULL;
    if (ref->released) return CONSTRAINT_ERR_DOUBLE_RELEASE;
    if (!ref->target) return CONSTRAINT_ERR_NULL;

    if (ref->target->constraint_count <= 0) {
        return CONSTRAINT_ERR_VIOLATION;
    }

    ref->target->constraint_count--;
    ref->released = true;

    /* Remove from sources list */
    for (int i = 0; i < ref->target->source_count; i++) {
        if (ref->target->constraint_sources[i] == ref->source) {
            /* Shift remaining sources */
            for (int j = i; j < ref->target->source_count - 1; j++) {
                ref->target->constraint_sources[j] = ref->target->constraint_sources[j + 1];
            }
            ref->target->source_count--;
            break;
        }
    }

    return CONSTRAINT_OK;
}

/* Free constraint reference struct */
void constraint_ref_free(ConstraintRef* ref) {
    free(ref);
}

/* Free constraint object */
ConstraintError constraint_free(ConstraintContext* ctx, ConstraintObj* obj) {
    if (!obj) return CONSTRAINT_ERR_NULL;
    if (obj->freed) return CONSTRAINT_ERR_DOUBLE_FREE;

    if (obj->constraint_count > 0) {
        /* Build violation message */
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "constraint violation: cannot free object [owner: %s] with %d active constraints",
                 obj->owner ? obj->owner : "unknown",
                 obj->constraint_count);

        if (ctx) {
            add_violation(ctx, msg);

            if (ctx->assert_on_error) {
                fprintf(stderr, "%s\n", msg);
                fprintf(stderr, "  Constraint sources:\n");
                for (int i = 0; i < obj->source_count; i++) {
                    fprintf(stderr, "    - %s\n", obj->constraint_sources[i]);
                }
                abort();
            }
        }

        return CONSTRAINT_ERR_VIOLATION;
    }

    obj->freed = true;
    if (obj->destructor && obj->data) {
        obj->destructor(obj->data);
    }
    obj->data = NULL;

    return CONSTRAINT_OK;
}

/* Dereference safely */
void* constraint_deref(ConstraintRef* ref, ConstraintError* err) {
    if (!ref || !ref->target) {
        if (err) *err = CONSTRAINT_ERR_NULL;
        return NULL;
    }
    if (ref->released) {
        if (err) *err = CONSTRAINT_ERR_RELEASED;
        return NULL;
    }
    if (ref->target->freed) {
        if (err) *err = CONSTRAINT_ERR_FREED;
        return NULL;
    }

    if (err) *err = CONSTRAINT_OK;
    return ref->target->data;
}

/* Check if reference is valid */
bool constraint_ref_is_valid(ConstraintRef* ref) {
    return ref && ref->target && !ref->released && !ref->target->freed;
}

/* Check if context has violations */
bool constraint_has_violations(ConstraintContext* ctx) {
    return ctx && ctx->violation_count > 0;
}

/* Get violation count */
int constraint_get_violation_count(ConstraintContext* ctx) {
    return ctx ? ctx->violation_count : 0;
}

/* Get violation by index */
const char* constraint_get_violation(ConstraintContext* ctx, int index) {
    if (!ctx || index < 0 || index >= ctx->violation_count) return NULL;
    return ctx->violations[index];
}

/* Clear violations */
void constraint_clear_violations(ConstraintContext* ctx) {
    if (!ctx) return;
    for (int i = 0; i < ctx->violation_count; i++) {
        free(ctx->violations[i]);
    }
    ctx->violation_count = 0;
}

/* Get statistics */
ConstraintStats constraint_get_stats(ConstraintContext* ctx) {
    ConstraintStats stats = {0, 0, 0, 0};
    if (!ctx) return stats;

    stats.total_objects = ctx->object_count;
    stats.violation_count = ctx->violation_count;

    for (int i = 0; i < ctx->object_count; i++) {
        ConstraintObj* obj = ctx->objects[i];
        if (obj && !obj->freed) {
            stats.active_objects++;
            stats.total_constraints += obj->constraint_count;
        }
    }

    return stats;
}
