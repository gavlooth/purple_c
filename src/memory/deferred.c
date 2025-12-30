#include "deferred.h"
#include <stdio.h>

// -- Context Management --

DeferredContext* mk_deferred_context(int batch_size) {
    DeferredContext* ctx = malloc(sizeof(DeferredContext));
    if (!ctx) return NULL;
    ctx->obj_lookup = hashmap_new();
    if (!ctx->obj_lookup) {
        free(ctx);
        return NULL;
    }
    ctx->pending = NULL;
    ctx->pending_count = 0;
    ctx->batch_size = batch_size > 0 ? batch_size : 32;
    ctx->total_deferred = 0;
    ctx->dropped_decrements = 0;
    return ctx;
}

void free_deferred_context(DeferredContext* ctx) {
    if (!ctx) return;
    DeferredDec* d = ctx->pending;
    while (d) {
        DeferredDec* next = d->next;
        free(d);
        d = next;
    }
    if (ctx->obj_lookup) {
        hashmap_free(ctx->obj_lookup);
    }
    free(ctx);
}

// -- Deferred Operations --

void defer_decrement(DeferredContext* ctx, void* obj) {
    if (!ctx || !obj) return;

    // O(1) lookup using hash map
    DeferredDec* d = (DeferredDec*)hashmap_get(ctx->obj_lookup, obj);
    if (d) {
        d->count++;
        return;
    }

    // Add new entry
    d = malloc(sizeof(DeferredDec));
    if (!d) {
        ctx->dropped_decrements++;
        return;  // Allocation failed
    }
    d->obj = obj;
    d->count = 1;
    d->next = ctx->pending;
    ctx->pending = d;
    ctx->pending_count++;
    ctx->total_deferred++;

    // Add to hash map for O(1) lookup
    hashmap_put(ctx->obj_lookup, obj, d);
}

void process_deferred(DeferredContext* ctx, int max_count) {
    if (!ctx || !ctx->pending) return;

    int processed = 0;
    DeferredDec** prev = &ctx->pending;

    while (*prev && processed < max_count) {
        DeferredDec* d = *prev;

        // Process one decrement for this object
        d->count--;
        processed++;

        if (d->count <= 0) {
            // Remove from hash map
            hashmap_remove(ctx->obj_lookup, d->obj);
            // Remove from list
            *prev = d->next;
            ctx->pending_count--;
            // Note: Actual freeing of d->obj handled by caller
            // This is just bookkeeping
            free(d);
        } else {
            prev = &d->next;
        }
    }
}

void flush_deferred(DeferredContext* ctx) {
    while (ctx && ctx->pending) {
        process_deferred(ctx, ctx->batch_size);
    }
}

int should_process_deferred(DeferredContext* ctx) {
    if (!ctx) return 0;
    return ctx->pending_count >= ctx->batch_size;
}

// -- Code Generation --

void gen_deferred_runtime(void) {
    printf("\n// Phase 7: Deferred RC Fallback Runtime\n");
    printf("// For mutable cycles that never freeze\n");
    printf("// Bounded O(k) processing at safe points\n\n");

    printf("typedef struct DeferredDec {\n");
    printf("    Obj* obj;\n");
    printf("    int count;\n");
    printf("    struct DeferredDec* next;      // For linked list\n");
    printf("    struct DeferredDec* hash_next; // For hash bucket chain\n");
    printf("} DeferredDec;\n\n");

    printf("#define DEFERRED_HASH_SIZE 256\n");
    printf("DeferredDec* DEFERRED_HASH[DEFERRED_HASH_SIZE];\n");
    printf("DeferredDec* DEFERRED_HEAD = NULL;\n");
    printf("int DEFERRED_COUNT = 0;\n");
    printf("#define DEFERRED_BATCH_SIZE 32\n\n");

    printf("static size_t deferred_hash_ptr(void* p) {\n");
    printf("    size_t x = (size_t)p;\n");
    printf("    x = ((x >> 16) ^ x) * 0x45d9f3b;\n");
    printf("    x = ((x >> 16) ^ x) * 0x45d9f3b;\n");
    printf("    return (x >> 16) ^ x;\n");
    printf("}\n\n");

    printf("void defer_dec(Obj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    size_t idx = deferred_hash_ptr(obj) %% DEFERRED_HASH_SIZE;\n");
    printf("    DeferredDec* d = DEFERRED_HASH[idx];\n");
    printf("    while (d) {\n");
    printf("        if (d->obj == obj) { d->count++; return; }\n");
    printf("        d = d->hash_next;\n");
    printf("    }\n");
    printf("    d = malloc(sizeof(DeferredDec));\n");
    printf("    if (!d) {\n");
    printf("        // OOM fallback: apply decrement immediately\n");
    printf("        obj->mark--;\n");
    printf("        if (obj->mark <= 0) {\n");
    printf("            if (obj->is_pair) {\n");
    printf("                if (obj->a) defer_dec(obj->a);\n");
    printf("                if (obj->b) defer_dec(obj->b);\n");
    printf("            }\n");
    printf("            invalidate_weak_refs_for(obj);\n");
    printf("            free(obj);\n");
    printf("        }\n");
    printf("        return;\n");
    printf("    }\n");
    printf("    d->obj = obj;\n");
    printf("    d->count = 1;\n");
    printf("    d->next = DEFERRED_HEAD;\n");
    printf("    d->hash_next = DEFERRED_HASH[idx];\n");
    printf("    DEFERRED_HEAD = d;\n");
    printf("    DEFERRED_HASH[idx] = d;\n");
    printf("    DEFERRED_COUNT++;\n");
    printf("}\n\n");

    printf("static void deferred_remove_from_hash(DeferredDec* d) {\n");
    printf("    size_t idx = deferred_hash_ptr(d->obj) %% DEFERRED_HASH_SIZE;\n");
    printf("    DeferredDec** hp = &DEFERRED_HASH[idx];\n");
    printf("    while (*hp) {\n");
    printf("        if (*hp == d) { *hp = d->hash_next; return; }\n");
    printf("        hp = &(*hp)->hash_next;\n");
    printf("    }\n");
    printf("}\n\n");

    printf("void process_deferred_batch(int max_count) {\n");
    printf("    int processed = 0;\n");
    printf("    DeferredDec** prev = &DEFERRED_HEAD;\n");
    printf("    while (*prev && processed < max_count) {\n");
    printf("        DeferredDec* d = *prev;\n");
    printf("        d->count--;\n");
    printf("        processed++;\n");
    printf("        if (d->count <= 0) {\n");
    printf("            *prev = d->next;\n");
    printf("            deferred_remove_from_hash(d);\n");
    printf("            DEFERRED_COUNT--;\n");
    printf("            // Apply actual decrement\n");
    printf("            d->obj->mark--;\n");
    printf("            if (d->obj->mark <= 0) {\n");
    printf("                // Object is dead, defer children\n");
    printf("                if (d->obj->is_pair) {\n");
    printf("                    if (d->obj->a) defer_dec(d->obj->a);\n");
    printf("                    if (d->obj->b) defer_dec(d->obj->b);\n");
    printf("                }\n");
    printf("                invalidate_weak_refs_for(d->obj);\n");
    printf("                free(d->obj);\n");
    printf("            }\n");
    printf("            free(d);\n");
    printf("        } else {\n");
    printf("            prev = &d->next;\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");

    printf("// Safe point: process deferred if threshold reached\n");
    printf("void safe_point() {\n");
    printf("    if (DEFERRED_COUNT >= DEFERRED_BATCH_SIZE) {\n");
    printf("        process_deferred_batch(DEFERRED_BATCH_SIZE);\n");
    printf("    }\n");
    printf("}\n\n");

    printf("// Flush all deferred at program end\n");
    printf("void flush_all_deferred() {\n");
    printf("    while (DEFERRED_HEAD) {\n");
    printf("        process_deferred_batch(DEFERRED_BATCH_SIZE);\n");
    printf("    }\n");
    printf("}\n\n");

    printf("// Deferred release for cyclic structures\n");
    printf("void deferred_release(Obj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    // For cyclic structures, use deferred decrement\n");
    printf("    defer_dec(obj);\n");
    printf("    // Process if threshold reached\n");
    printf("    safe_point();\n");
    printf("}\n\n");
}

void gen_safe_point(const char* location) {
    printf("    safe_point(); // %s\n", location ? location : "safe point");
}
