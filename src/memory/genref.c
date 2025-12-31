/*
 * Random Generational References implementation
 * Vale-style use-after-free detection
 */

#include "genref.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

/* Platform-specific random */
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

/* Helper: grow array */
static void* grow_array(void* arr, int* capacity, size_t elem_size) {
    int new_cap;
    if (*capacity == 0) {
        new_cap = 8;
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

/* Generate cryptographically random 64-bit generation */
Generation genref_random_generation(void) {
    Generation gen = 0;

#ifdef _WIN32
    HCRYPTPROV hProv;
    if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hProv, sizeof(gen), (BYTE*)&gen);
        CryptReleaseContext(hProv, 0);
    }
#else
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t bytes_read = read(fd, &gen, sizeof(gen));
        close(fd);
        if (bytes_read != sizeof(gen)) {
            gen = 0;
        }
    }
#endif

    /* Fallback if random failed - use time-based mixing */
    if (gen == 0) {
        static uint64_t counter = 0;
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        gen = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        gen ^= (++counter * 0x9e3779b97f4a7c15ULL);  /* Mix with counter */
        gen ^= (gen >> 33);
        gen *= 0xff51afd7ed558ccdULL;
        gen ^= (gen >> 33);
    }

    /* Ensure non-zero (0 means invalid) */
    if (gen == 0) gen = 1;

    return gen;
}

/* Create new context */
GenRefContext* genref_context_new(void) {
    GenRefContext* ctx = calloc(1, sizeof(GenRefContext));
    if (!ctx) return NULL;

    ctx->objects = NULL;
    ctx->object_count = 0;
    ctx->object_capacity = 0;

    return ctx;
}

/* Free context (does not free objects - they may be freed elsewhere) */
void genref_context_free(GenRefContext* ctx) {
    if (!ctx) return;
    free(ctx->objects);
    free(ctx);
}

/* Allocate new object with random generation */
GenObj* genref_alloc(GenRefContext* ctx, void* data, void (*destructor)(void*)) {
    GenObj* obj = calloc(1, sizeof(GenObj));
    if (!obj) return NULL;

    obj->generation = genref_random_generation();
    obj->data = data;
    obj->destructor = destructor;
    obj->freed = false;

    /* Add to context if provided */
    if (ctx) {
        if (ctx->object_count >= ctx->object_capacity) {
            GenObj** new_objs = grow_array(
                ctx->objects,
                &ctx->object_capacity,
                sizeof(GenObj*)
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

/* Free an object - zeros generation to invalidate all references */
void genref_free(GenObj* obj) {
    if (!obj) return;
    if (obj->freed) return;

    obj->generation = 0;  /* Invalidate all existing pointers */
    obj->freed = true;

    if (obj->destructor && obj->data) {
        obj->destructor(obj->data);
    }
    obj->data = NULL;
}

/* Create a generational reference to an object */
GenRef* genref_create_ref(GenObj* obj, const char* source_desc) {
    if (!obj) return NULL;
    if (obj->freed || obj->generation == 0) return NULL;

    GenRef* ref = calloc(1, sizeof(GenRef));
    if (!ref) return NULL;

    ref->target = obj;
    ref->remembered_gen = obj->generation;
    ref->source_desc = source_desc;

    return ref;
}

/* Free a reference */
void genref_ref_free(GenRef* ref) {
    free(ref);
}

/* Dereference safely - returns NULL and sets error on UAF */
void* genref_deref(GenRef* ref, GenRefError* err) {
    if (!ref || !ref->target) {
        if (err) *err = GENREF_ERR_NULL;
        return NULL;
    }

    /* The key check: remembered generation must match current generation */
    if (ref->remembered_gen != ref->target->generation) {
        if (ref->target->generation == 0) {
            if (err) *err = GENREF_ERR_UAF;
            fprintf(stderr, "use-after-free detected: object was freed (gen 0), "
                    "ref remembered gen %lu [created at: %s]\n",
                    (unsigned long)ref->remembered_gen,
                    ref->source_desc ? ref->source_desc : "unknown");
        } else {
            if (err) *err = GENREF_ERR_UAF;
            fprintf(stderr, "use-after-free detected: generation mismatch "
                    "(obj: %lu, ref: %lu) [created at: %s]\n",
                    (unsigned long)ref->target->generation,
                    (unsigned long)ref->remembered_gen,
                    ref->source_desc ? ref->source_desc : "unknown");
        }
        return NULL;
    }

    if (err) *err = GENREF_OK;
    return ref->target->data;
}

/* Check if reference is valid (O(1)) */
bool genref_is_valid(GenRef* ref) {
    if (!ref || !ref->target) return false;
    return ref->remembered_gen == ref->target->generation &&
           ref->target->generation != 0;
}

/* Create closure with captured references */
GenClosure* genref_closure_new(GenRef** captures, int count, void* (*fn)(void*), void* context) {
    GenClosure* closure = calloc(1, sizeof(GenClosure));
    if (!closure) return NULL;

    if (count > 0 && captures) {
        closure->captures = calloc(count, sizeof(GenRef*));
        if (!closure->captures) {
            free(closure);
            return NULL;
        }
        for (int i = 0; i < count; i++) {
            closure->captures[i] = captures[i];
        }
        closure->capture_count = count;
    }

    closure->fn = fn;
    closure->context = context;

    return closure;
}

/* Free closure (does not free captured refs - caller's responsibility) */
void genref_closure_free(GenClosure* closure) {
    if (!closure) return;
    free(closure->captures);
    free(closure);
}

/* Validate all captures */
GenRefError genref_closure_validate(GenClosure* closure) {
    if (!closure) return GENREF_ERR_NULL;

    for (int i = 0; i < closure->capture_count; i++) {
        if (!genref_is_valid(closure->captures[i])) {
            fprintf(stderr, "closure capture %d is invalid [created at: %s]\n",
                    i, closure->captures[i] ? closure->captures[i]->source_desc : "null");
            return GENREF_ERR_INVALID_CAPTURE;
        }
    }

    return GENREF_OK;
}

/* Call closure after validating captures */
void* genref_closure_call(GenClosure* closure, GenRefError* err) {
    GenRefError validate_err = genref_closure_validate(closure);
    if (validate_err != GENREF_OK) {
        if (err) *err = validate_err;
        return NULL;
    }

    if (err) *err = GENREF_OK;
    return closure->fn(closure->context);
}
