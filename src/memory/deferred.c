#include "deferred.h"
#include <stdio.h>

// -- Context Management --

DeferredContext* mk_deferred_context(int batch_size) {
    DeferredContext* ctx = malloc(sizeof(DeferredContext));
    ctx->pending = NULL;
    ctx->pending_count = 0;
    ctx->batch_size = batch_size > 0 ? batch_size : 32;
    ctx->total_deferred = 0;
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
    free(ctx);
}

// -- Deferred Operations --

void defer_decrement(DeferredContext* ctx, void* obj) {
    if (!ctx || !obj) return;

    // Check if already in pending list
    DeferredDec* d = ctx->pending;
    while (d) {
        if (d->obj == obj) {
            d->count++;
            return;
        }
        d = d->next;
    }

    // Add new entry
    d = malloc(sizeof(DeferredDec));
    d->obj = obj;
    d->count = 1;
    d->next = ctx->pending;
    ctx->pending = d;
    ctx->pending_count++;
    ctx->total_deferred++;
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
    printf("    struct DeferredDec* next;\n");
    printf("} DeferredDec;\n\n");

    printf("DeferredDec* DEFERRED_HEAD = NULL;\n");
    printf("int DEFERRED_COUNT = 0;\n");
    printf("#define DEFERRED_BATCH_SIZE 32\n\n");

    printf("void defer_dec(Obj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    DeferredDec* d = DEFERRED_HEAD;\n");
    printf("    while (d) {\n");
    printf("        if (d->obj == obj) { d->count++; return; }\n");
    printf("        d = d->next;\n");
    printf("    }\n");
    printf("    d = malloc(sizeof(DeferredDec));\n");
    printf("    d->obj = obj;\n");
    printf("    d->count = 1;\n");
    printf("    d->next = DEFERRED_HEAD;\n");
    printf("    DEFERRED_HEAD = d;\n");
    printf("    DEFERRED_COUNT++;\n");
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
    printf("            DEFERRED_COUNT--;\n");
    printf("            // Apply actual decrement\n");
    printf("            d->obj->mark--;\n");
    printf("            if (d->obj->mark <= 0) {\n");
    printf("                // Object is dead, defer children\n");
    printf("                if (d->obj->a) defer_dec(d->obj->a);\n");
    printf("                if (d->obj->b) defer_dec(d->obj->b);\n");
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
