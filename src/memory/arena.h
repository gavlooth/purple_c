#ifndef PURPLE_ARENA_H
#define PURPLE_ARENA_H

#include "../types.h"

// -- Phase 8: Arena Allocation --
// For cyclic structures that don't escape scope
// Bulk allocation and O(1) deallocation
// Constraint: per-scope cleanup only; no stop-the-world.

// Arena block for allocation
typedef struct ArenaBlock {
    char* memory;
    size_t size;
    size_t used;
    struct ArenaBlock* next;
} ArenaBlock;

// Arena allocator
typedef struct Arena {
    ArenaBlock* current;
    ArenaBlock* blocks;
    size_t block_size;
    int id;
    struct ArenaExternal* externals;
} Arena;

typedef void (*ArenaReleaseFn)(void*);

typedef struct ArenaExternal {
    void* ptr;
    ArenaReleaseFn release;
    struct ArenaExternal* next;
} ArenaExternal;

// Arena scope tracking
typedef struct ArenaScope {
    int id;
    int start_line;
    int end_line;
    char** allocated_vars;
    int var_count;
    int is_cyclic;
    struct ArenaScope* next;
} ArenaScope;

// Arena management
Arena* arena_create(size_t block_size);
void* arena_alloc(Arena* arena, size_t size);
void arena_destroy(Arena* arena);
void arena_reset(Arena* arena);
void arena_register_external(Arena* arena, void* ptr, ArenaReleaseFn release);
void arena_release_externals(Arena* arena);

// Scope detection
ArenaScope* find_arena_scopes(Value* expr);
int should_use_arena(const char* var_name, ArenaScope* scopes);

// Code generation
void gen_arena_runtime(void);
void gen_arena_scope_begin(int scope_id);
void gen_arena_scope_end(int scope_id);
void gen_arena_alloc(int scope_id, const char* var_name, const char* type);

#endif // PURPLE_ARENA_H
