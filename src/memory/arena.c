#include "arena.h"
#include <stdio.h>
#include <string.h>

// -- Arena Management --

Arena* arena_create(size_t block_size) {
    Arena* a = malloc(sizeof(Arena));
    if (!a) return NULL;
    a->block_size = block_size > 0 ? block_size : 4096;
    a->blocks = NULL;
    a->current = NULL;
    a->id = 0;
    a->externals = NULL;
    return a;
}

void* arena_alloc(Arena* a, size_t size) {
    if (!a) return NULL;

    // Align to 8 bytes
    size = (size + 7) & ~(size_t)7;

    if (!a->current || a->current->used + size > a->current->size) {
        // Need new block
        size_t bs = a->block_size;
        if (size > bs) bs = size;

        ArenaBlock* b = malloc(sizeof(ArenaBlock));
        if (!b) return NULL;
        b->memory = malloc(bs);
        if (!b->memory) {
            free(b);
            return NULL;
        }
        b->size = bs;
        b->used = 0;
        b->next = a->blocks;
        a->blocks = b;
        a->current = b;
    }

    void* ptr = a->current->memory + a->current->used;
    a->current->used += size;
    return ptr;
}

void arena_destroy(Arena* a) {
    if (!a) return;

    arena_release_externals(a);

    ArenaBlock* b = a->blocks;
    while (b) {
        ArenaBlock* next = b->next;
        free(b->memory);
        free(b);
        b = next;
    }
    free(a);
}

void arena_reset(Arena* a) {
    if (!a) return;

    ArenaBlock* b = a->blocks;
    while (b) {
        b->used = 0;
        b = b->next;
    }
    a->current = a->blocks;
}

void arena_register_external(Arena* a, void* ptr, ArenaReleaseFn release) {
    if (!a || !ptr || !release) return;
    ArenaExternal* ext = malloc(sizeof(ArenaExternal));
    if (!ext) return;
    ext->ptr = ptr;
    ext->release = release;
    ext->next = a->externals;
    a->externals = ext;
}

void arena_release_externals(Arena* a) {
    if (!a) return;
    ArenaExternal* ext = a->externals;
    while (ext) {
        ArenaExternal* next = ext->next;
        ext->release(ext->ptr);
        free(ext);
        ext = next;
    }
    a->externals = NULL;
}

// -- Scope Detection --

ArenaScope* find_arena_scopes(Value* expr) {
    // Look for let bindings with cyclic shapes that don't escape
    // For now, return NULL - full implementation would analyze the AST
    (void)expr;
    return NULL;
}

int should_use_arena(const char* var_name, ArenaScope* scopes) {
    ArenaScope* s = scopes;
    while (s) {
        for (int i = 0; i < s->var_count; i++) {
            if (strcmp(s->allocated_vars[i], var_name) == 0) {
                return s->id;
            }
        }
        s = s->next;
    }
    return 0;
}

// -- Code Generation --

void gen_arena_runtime(void) {
    printf("\n// Phase 8: Arena Allocator for Cyclic Structures\n");
    printf("// Bulk allocation, O(1) deallocation\n\n");

    printf("typedef struct ArenaBlock {\n");
    printf("    char* memory;\n");
    printf("    size_t size;\n");
    printf("    size_t used;\n");
    printf("    struct ArenaBlock* next;\n");
    printf("} ArenaBlock;\n\n");

    printf("typedef struct Arena {\n");
    printf("    ArenaBlock* current;\n");
    printf("    ArenaBlock* blocks;\n");
    printf("    size_t block_size;\n");
    printf("    struct ArenaExternal* externals;\n");
    printf("} Arena;\n\n");

    printf("typedef void (*ArenaReleaseFn)(void*);\n");
    printf("typedef struct ArenaExternal {\n");
    printf("    void* ptr;\n");
    printf("    ArenaReleaseFn release;\n");
    printf("    struct ArenaExternal* next;\n");
    printf("} ArenaExternal;\n\n");

    printf("Arena* arena_create(size_t block_size) {\n");
    printf("    Arena* a = malloc(sizeof(Arena));\n");
    printf("    if (!a) return NULL;\n");
    printf("    a->block_size = block_size ? block_size : 4096;\n");
    printf("    a->blocks = NULL;\n");
    printf("    a->current = NULL;\n");
    printf("    a->externals = NULL;\n");
    printf("    return a;\n");
    printf("}\n\n");

    printf("void arena_register_external(Arena* a, void* ptr, ArenaReleaseFn release) {\n");
    printf("    if (!a || !ptr || !release) return;\n");
    printf("    ArenaExternal* ext = malloc(sizeof(ArenaExternal));\n");
    printf("    if (!ext) return;\n");
    printf("    ext->ptr = ptr;\n");
    printf("    ext->release = release;\n");
    printf("    ext->next = a->externals;\n");
    printf("    a->externals = ext;\n");
    printf("}\n\n");

    printf("void arena_release_externals(Arena* a) {\n");
    printf("    if (!a) return;\n");
    printf("    ArenaExternal* ext = a->externals;\n");
    printf("    while (ext) {\n");
    printf("        ArenaExternal* next = ext->next;\n");
    printf("        ext->release(ext->ptr);\n");
    printf("        free(ext);\n");
    printf("        ext = next;\n");
    printf("    }\n");
    printf("    a->externals = NULL;\n");
    printf("}\n\n");

    printf("void* arena_alloc(Arena* a, size_t size) {\n");
    printf("    if (!a) return NULL;\n");
    printf("    size = (size + 7) & ~(size_t)7;\n");
    printf("    if (!a->current || a->current->used + size > a->current->size) {\n");
    printf("        size_t bs = a->block_size;\n");
    printf("        if (size > bs) bs = size;\n");
    printf("        ArenaBlock* b = malloc(sizeof(ArenaBlock));\n");
    printf("        if (!b) return NULL;\n");
    printf("        b->memory = malloc(bs);\n");
    printf("        if (!b->memory) { free(b); return NULL; }\n");
    printf("        b->size = bs;\n");
    printf("        b->used = 0;\n");
    printf("        b->next = a->blocks;\n");
    printf("        a->blocks = b;\n");
    printf("        a->current = b;\n");
    printf("    }\n");
    printf("    void* ptr = a->current->memory + a->current->used;\n");
    printf("    a->current->used += size;\n");
    printf("    return ptr;\n");
    printf("}\n\n");

    printf("void arena_destroy(Arena* a) {\n");
    printf("    if (!a) return;\n");
    printf("    arena_release_externals(a);\n");
    printf("    ArenaBlock* b = a->blocks;\n");
    printf("    while (b) {\n");
    printf("        ArenaBlock* next = b->next;\n");
    printf("        free(b->memory);\n");
    printf("        free(b);\n");
    printf("        b = next;\n");
    printf("    }\n");
    printf("    free(a);\n");
    printf("}\n\n");

    // Arena-aware allocators
    printf("// Arena-aware allocators\n");
    printf("Obj* arena_mk_int(Arena* a, long val) {\n");
    printf("    Obj* o = arena_alloc(a, sizeof(Obj));\n");
    printf("    if (!o) return NULL;\n");
    printf("    o->mark = 1; o->scc_id = -1; o->is_pair = 0; o->scan_tag = 0;\n");
    printf("    o->i = val;\n");
    printf("    return o;\n");
    printf("}\n\n");

    printf("Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr) {\n");
    printf("    Obj* o = arena_alloc(a, sizeof(Obj));\n");
    printf("    if (!o) return NULL;\n");
    printf("    o->mark = 1; o->scc_id = -1; o->is_pair = 1; o->scan_tag = 0;\n");
    printf("    o->a = car; o->b = cdr;\n");
    printf("    return o;\n");
    printf("}\n\n");
}

void gen_arena_scope_begin(int scope_id) {
    printf("    // ARENA SCOPE %d begin - cyclic allocations\n", scope_id);
    printf("    Arena* _arena_%d = arena_create(0);\n", scope_id);
}

void gen_arena_scope_end(int scope_id) {
    printf("    arena_destroy(_arena_%d);  // O(1) bulk free\n", scope_id);
    printf("    // ARENA SCOPE %d end\n", scope_id);
}

void gen_arena_alloc(int scope_id, const char* var_name, const char* type) {
    if (strcmp(type, "int") == 0) {
        printf("    Obj* %s = arena_mk_int(_arena_%d, 0);\n", var_name, scope_id);
    } else {
        printf("    Obj* %s = arena_mk_pair(_arena_%d, NULL, NULL);\n", var_name, scope_id);
    }
}
