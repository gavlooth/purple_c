/*
 * Region References implementation
 * Vale/Ada/SPARK-style scope hierarchy validation
 */

#include "region.h"
#include <stdlib.h>
#include <stdatomic.h>
#include <limits.h>

/* Global region ID counter */
static atomic_uint_fast64_t next_region_id = 1;

/* Helper: grow array */
static void* grow_array(void* arr, int* capacity, size_t elem_size) {
    int new_cap;
    if (*capacity == 0) {
        new_cap = 4;
    } else {
        /* Check for integer overflow before doubling */
        if (*capacity > INT_MAX / 2) {
            return NULL;  /* Cannot grow - would overflow */
        }
        new_cap = *capacity * 2;
    }
    void* new_arr = realloc(arr, new_cap * elem_size);
    if (new_arr) {
        *capacity = new_cap;
    }
    return new_arr;
}

/* Create a new region */
static Region* region_new(Region* parent) {
    Region* r = calloc(1, sizeof(Region));
    if (!r) return NULL;

    r->id = atomic_fetch_add(&next_region_id, 1);
    r->depth = parent ? parent->depth + 1 : 0;
    r->parent = parent;
    r->children = NULL;
    r->child_count = 0;
    r->child_capacity = 0;
    r->objects = NULL;
    r->object_count = 0;
    r->object_capacity = 0;
    r->closed = false;

    return r;
}

/* Free a region (internal) */
static void region_destroy(Region* r) {
    if (!r) return;

    /* Free all objects */
    for (int i = 0; i < r->object_count; i++) {
        RegionObj* obj = r->objects[i];
        if (obj) {
            if (obj->destructor && obj->data) {
                obj->destructor(obj->data);
            }
            /* Free refs array */
            free(obj->refs);
            free(obj);
        }
    }
    free(r->objects);

    /* Free children recursively */
    for (int i = 0; i < r->child_count; i++) {
        region_destroy(r->children[i]);
    }
    free(r->children);

    free(r);
}

/* Create new context */
RegionContext* region_context_new(void) {
    RegionContext* ctx = calloc(1, sizeof(RegionContext));
    if (!ctx) return NULL;

    ctx->root = region_new(NULL);
    if (!ctx->root) {
        free(ctx);
        return NULL;
    }

    ctx->current = ctx->root;
    return ctx;
}

/* Free context and all regions */
void region_context_free(RegionContext* ctx) {
    if (!ctx) return;
    region_destroy(ctx->root);
    free(ctx);
}

/* Enter a new child region */
Region* region_enter(RegionContext* ctx) {
    if (!ctx || !ctx->current) return NULL;

    Region* child = region_new(ctx->current);
    if (!child) return NULL;

    /* Add to parent's children */
    if (ctx->current->child_count >= ctx->current->child_capacity) {
        Region** new_children = grow_array(
            ctx->current->children,
            &ctx->current->child_capacity,
            sizeof(Region*)
        );
        if (!new_children) {
            free(child);
            return NULL;
        }
        ctx->current->children = new_children;
    }
    ctx->current->children[ctx->current->child_count++] = child;

    ctx->current = child;
    return child;
}

/* Exit current region */
RegionError region_exit(RegionContext* ctx) {
    if (!ctx || !ctx->current) return REGION_ERR_NULL;
    if (ctx->current == ctx->root) return REGION_ERR_CANNOT_EXIT_ROOT;
    if (ctx->current->closed) return REGION_ERR_CLOSED;

    /* Mark as closed and invalidate all objects */
    ctx->current->closed = true;
    for (int i = 0; i < ctx->current->object_count; i++) {
        RegionObj* obj = ctx->current->objects[i];
        if (obj) {
            obj->region = NULL;  /* Mark as invalid */
        }
    }

    /* Return to parent */
    ctx->current = ctx->current->parent;
    return REGION_OK;
}

/* Allocate object in current region */
RegionObj* region_alloc(RegionContext* ctx, void* data, void (*destructor)(void*)) {
    if (!ctx || !ctx->current) return NULL;
    if (ctx->current->closed) return NULL;

    RegionObj* obj = calloc(1, sizeof(RegionObj));
    if (!obj) return NULL;

    obj->region = ctx->current;
    obj->data = data;
    obj->destructor = destructor;
    obj->refs = NULL;
    obj->ref_count = 0;
    obj->ref_capacity = 0;

    /* Add to region's objects */
    if (ctx->current->object_count >= ctx->current->object_capacity) {
        RegionObj** new_objects = grow_array(
            ctx->current->objects,
            &ctx->current->object_capacity,
            sizeof(RegionObj*)
        );
        if (!new_objects) {
            free(obj);
            return NULL;
        }
        ctx->current->objects = new_objects;
    }
    ctx->current->objects[ctx->current->object_count++] = obj;

    return obj;
}

/* Create reference from source to target */
RegionError region_create_ref(RegionContext* ctx, RegionObj* source, RegionObj* target, RegionRef** out_ref) {
    (void)ctx;  /* Unused but kept for API consistency */

    if (!source || !target || !out_ref) return REGION_ERR_NULL;
    if (!source->region) return REGION_ERR_CLOSED;
    if (!target->region) return REGION_ERR_CLOSED;

    /* Key check: source cannot point to more deeply scoped target */
    /* Allowed: inner → outer (source.depth >= target.depth) */
    /* Forbidden: outer → inner (source.depth < target.depth) */
    if (source->region->depth < target->region->depth) {
        return REGION_ERR_SCOPE_VIOLATION;
    }

    RegionRef* ref = calloc(1, sizeof(RegionRef));
    if (!ref) return REGION_ERR_ALLOC_FAILED;

    ref->target = target;
    ref->source_region = source->region;

    /* Add to source's refs */
    if (source->ref_count >= source->ref_capacity) {
        RegionRef** new_refs = grow_array(
            source->refs,
            &source->ref_capacity,
            sizeof(RegionRef*)
        );
        if (!new_refs) {
            free(ref);
            return REGION_ERR_ALLOC_FAILED;
        }
        source->refs = new_refs;
    }
    source->refs[source->ref_count++] = ref;

    *out_ref = ref;
    return REGION_OK;
}

/* Dereference safely */
void* region_ref_deref(RegionRef* ref, RegionError* err) {
    if (!ref || !ref->target) {
        if (err) *err = REGION_ERR_NULL;
        return NULL;
    }
    if (!ref->target->region) {
        if (err) *err = REGION_ERR_CLOSED;
        return NULL;
    }

    if (err) *err = REGION_OK;
    return ref->target->data;
}

/* Check if reference is valid */
bool region_ref_is_valid(RegionRef* ref) {
    return ref && ref->target && ref->target->region && !ref->target->region->closed;
}

/* Check if source can reference target */
bool region_can_reference(RegionObj* source, RegionObj* target) {
    if (!source || !target) return false;
    if (!source->region || !target->region) return false;
    return source->region->depth >= target->region->depth;
}

/* Check if ancestor is ancestor of descendant */
bool region_is_ancestor(Region* ancestor, Region* descendant) {
    Region* current = descendant;
    while (current) {
        if (current == ancestor) return true;
        current = current->parent;
    }
    return false;
}

/* Get region depth */
RegionDepth region_get_depth(Region* r) {
    return r ? r->depth : 0;
}

/* Get object count */
int region_get_object_count(Region* r) {
    return r ? r->object_count : 0;
}

/* Check if closed */
bool region_is_closed(Region* r) {
    return r ? r->closed : true;
}
