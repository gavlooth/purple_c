/*
 * Region References - Vale/Ada/SPARK-style scope hierarchy validation
 *
 * Key invariant: A pointer cannot point to an object in a more deeply scoped region.
 * This prevents dangling references when inner scopes exit.
 *
 * Region hierarchy:
 *   Region A (depth 0)
 *     └── Region B (depth 1)
 *           └── Region C (depth 2)
 *
 * Allowed: C → B, C → A, B → A (inner can point to outer)
 * Forbidden: A → B, A → C, B → C (outer cannot point to inner)
 */

#ifndef REGION_H
#define REGION_H

#include <stdint.h>
#include <stdbool.h>

/* Region ID type */
typedef uint64_t RegionID;

/* Region depth (0 = outermost) */
typedef uint32_t RegionDepth;

/* Forward declarations */
typedef struct Region Region;
typedef struct RegionObj RegionObj;
typedef struct RegionRef RegionRef;
typedef struct RegionContext RegionContext;

/* Region - an isolated memory region with scope hierarchy */
struct Region {
    RegionID id;
    RegionDepth depth;
    Region* parent;
    Region** children;
    int child_count;
    int child_capacity;
    RegionObj** objects;
    int object_count;
    int object_capacity;
    bool closed;
};

/* RegionObj - an object allocated within a region */
struct RegionObj {
    Region* region;
    void* data;
    void (*destructor)(void*);
    RegionRef** refs;
    int ref_count;
    int ref_capacity;
};

/* RegionRef - a reference that carries region information */
struct RegionRef {
    RegionObj* target;
    Region* source_region;
};

/* RegionContext - manages the region hierarchy */
struct RegionContext {
    Region* root;
    Region* current;
};

/* Error codes */
typedef enum {
    REGION_OK = 0,
    REGION_ERR_NULL = -1,
    REGION_ERR_CLOSED = -2,
    REGION_ERR_SCOPE_VIOLATION = -3,
    REGION_ERR_CANNOT_EXIT_ROOT = -4,
    REGION_ERR_ALLOC_FAILED = -5
} RegionError;

/* Context management */
RegionContext* region_context_new(void);
void region_context_free(RegionContext* ctx);

/* Region operations */
Region* region_enter(RegionContext* ctx);
RegionError region_exit(RegionContext* ctx);
RegionDepth region_get_depth(Region* r);
int region_get_object_count(Region* r);
bool region_is_closed(Region* r);

/* Object operations */
RegionObj* region_alloc(RegionContext* ctx, void* data, void (*destructor)(void*));
RegionError region_create_ref(RegionContext* ctx, RegionObj* source, RegionObj* target, RegionRef** out_ref);

/* Reference operations */
void* region_ref_deref(RegionRef* ref, RegionError* err);
bool region_ref_is_valid(RegionRef* ref);

/* Utility functions */
bool region_can_reference(RegionObj* source, RegionObj* target);
bool region_is_ancestor(Region* ancestor, Region* descendant);

#endif /* REGION_H */
