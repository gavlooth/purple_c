#define _POSIX_C_SOURCE 200809L
// Phase 10: Exception Handling Implementation
// LLVM-style landing pads with ASAP cleanup

#include "exception.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int next_pad_id = 0;

// Track a new allocation
void track_alloc(ExceptionContext* ctx, const char* var, const char* type) {
    if (!ctx || !var || !type) return;

    LiveAlloc* alloc = malloc(sizeof(LiveAlloc));
    if (!alloc) return;
    alloc->var_name = strdup(var);
    alloc->type_name = strdup(type);
    if (!alloc->var_name || !alloc->type_name) {
        free(alloc->var_name);
        free(alloc->type_name);
        free(alloc);
        return;
    }
    alloc->program_point = 0;  // Would be set by caller
    alloc->next = ctx->live_allocs;
    ctx->live_allocs = alloc;
}

// Remove allocation from tracking
void untrack_alloc(ExceptionContext* ctx, const char* var) {
    if (!ctx || !var) return;

    LiveAlloc** prev = &ctx->live_allocs;
    while (*prev) {
        if (strcmp((*prev)->var_name, var) == 0) {
            LiveAlloc* to_free = *prev;
            *prev = (*prev)->next;
            free(to_free->var_name);
            free(to_free->type_name);
            free(to_free);
            return;
        }
        prev = &(*prev)->next;
    }
}

// Get live allocations at current point
LiveAlloc* get_live_allocs(ExceptionContext* ctx) {
    return ctx ? ctx->live_allocs : NULL;
}

// Create landing pad for current context
LandingPad* create_landing_pad(ExceptionContext* ctx) {
    if (!ctx) return NULL;

    LandingPad* pad = malloc(sizeof(LandingPad));
    if (!pad) return NULL;
    pad->id = next_pad_id++;
    pad->try_start = 0;
    pad->try_end = 0;
    pad->cleanups = NULL;
    pad->next = NULL;

    // Create cleanup actions for all live allocations
    LiveAlloc* alloc = ctx->live_allocs;
    while (alloc) {
        CleanupAction* action = malloc(sizeof(CleanupAction));
        if (!action) break;  // Stop on allocation failure
        action->var_name = strdup(alloc->var_name);
        action->cleanup_fn = strdup("dec_ref");
        if (!action->var_name || !action->cleanup_fn) {
            free(action->var_name);
            free(action->cleanup_fn);
            free(action);
            break;
        }

        action->next = pad->cleanups;
        pad->cleanups = action;

        alloc = alloc->next;
    }

    ctx->current_pad = pad;
    return pad;
}

// Generate exception runtime
void gen_exception_runtime(void) {
    printf("\n// Phase 10: Exception Handling Runtime\n");
    printf("// LLVM-style landing pads with ASAP cleanup\n\n");

    // Exception state
    printf("#include <setjmp.h>\n\n");

    printf("// Exception types\n");
    printf("typedef enum {\n");
    printf("    EXC_NONE = 0,\n");
    printf("    EXC_RUNTIME_ERROR,\n");
    printf("    EXC_OUT_OF_MEMORY,\n");
    printf("    EXC_USER_DEFINED\n");
    printf("} ExceptionType;\n\n");

    printf("// Exception value\n");
    printf("typedef struct Exception {\n");
    printf("    ExceptionType type;\n");
    printf("    const char* message;\n");
    printf("    void* data;\n");
    printf("} Exception;\n\n");

    // Exception context stack
    printf("// Exception context for nested try/catch\n");
    printf("typedef struct ExcFrame {\n");
    printf("    jmp_buf env;\n");
    printf("    Exception exc;\n");
    printf("    struct ExcFrame* prev;\n");
    printf("    void** cleanup_vars;     // Variables to clean up\n");
    printf("    int cleanup_count;\n");
    printf("    int cleanup_capacity;\n");
    printf("} ExcFrame;\n\n");

    printf("ExcFrame* EXC_STACK = NULL;\n\n");

    // Push exception frame
    printf("// Enter try block\n");
    printf("ExcFrame* exc_push() {\n");
    printf("    ExcFrame* frame = malloc(sizeof(ExcFrame));\n");
    printf("    if (!frame) return NULL;\n");
    printf("    frame->exc.type = EXC_NONE;\n");
    printf("    frame->exc.message = NULL;\n");
    printf("    frame->exc.data = NULL;\n");
    printf("    frame->prev = EXC_STACK;\n");
    printf("    frame->cleanup_vars = malloc(16 * sizeof(void*));\n");
    printf("    if (!frame->cleanup_vars) { free(frame); return NULL; }\n");
    printf("    frame->cleanup_count = 0;\n");
    printf("    frame->cleanup_capacity = 16;\n");
    printf("    EXC_STACK = frame;\n");
    printf("    return frame;\n");
    printf("}\n\n");

    // Pop exception frame
    printf("// Exit try block normally\n");
    printf("void exc_pop() {\n");
    printf("    if (!EXC_STACK) return;\n");
    printf("    ExcFrame* frame = EXC_STACK;\n");
    printf("    EXC_STACK = frame->prev;\n");
    printf("    free(frame->cleanup_vars);\n");
    printf("    free(frame);\n");
    printf("}\n\n");

    // Register cleanup
    printf("// Register variable for cleanup on exception\n");
    printf("void exc_register_cleanup(void* ptr) {\n");
    printf("    if (!EXC_STACK || !ptr) return;\n");
    printf("    ExcFrame* frame = EXC_STACK;\n");
    printf("    if (frame->cleanup_count >= frame->cleanup_capacity) {\n");
    printf("        if (frame->cleanup_capacity > INT_MAX / 2) return;  // Overflow protection\n");
    printf("        int new_cap = frame->cleanup_capacity * 2;\n");
    printf("        void** tmp = realloc(frame->cleanup_vars,\n");
    printf("            new_cap * sizeof(void*));\n");
    printf("        if (!tmp) return;  // Cannot register, but don't crash\n");
    printf("        frame->cleanup_vars = tmp;\n");
    printf("        frame->cleanup_capacity = new_cap;\n");
    printf("    }\n");
    printf("    frame->cleanup_vars[frame->cleanup_count++] = ptr;\n");
    printf("}\n\n");

    // Unregister cleanup (after normal free)
    printf("// Unregister variable (after normal free)\n");
    printf("void exc_unregister_cleanup(void* ptr) {\n");
    printf("    if (!EXC_STACK || !ptr) return;\n");
    printf("    ExcFrame* frame = EXC_STACK;\n");
    printf("    for (int i = 0; i < frame->cleanup_count; i++) {\n");
    printf("        if (frame->cleanup_vars[i] == ptr) {\n");
    printf("            frame->cleanup_vars[i] = frame->cleanup_vars[--frame->cleanup_count];\n");
    printf("            return;\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");

    // Run cleanups (landing pad)
    printf("// Landing pad: clean up all registered variables\n");
    printf("void exc_run_cleanups() {\n");
    printf("    if (!EXC_STACK) return;\n");
    printf("    ExcFrame* frame = EXC_STACK;\n");
    printf("    // Clean up in reverse order (LIFO)\n");
    printf("    for (int i = frame->cleanup_count - 1; i >= 0; i--) {\n");
    printf("        Obj* obj = (Obj*)frame->cleanup_vars[i];\n");
    printf("        if (obj) dec_ref(obj);\n");
    printf("    }\n");
    printf("    frame->cleanup_count = 0;\n");
    printf("}\n\n");

    // Throw exception
    printf("// Throw an exception\n");
    printf("void exc_throw(ExceptionType type, const char* message) {\n");
    printf("    if (!EXC_STACK) {\n");
    printf("        fprintf(stderr, \"Uncaught exception: %%s\\n\", message);\n");
    printf("        exit(1);\n");
    printf("    }\n");
    printf("    EXC_STACK->exc.type = type;\n");
    printf("    EXC_STACK->exc.message = message;\n");
    printf("    exc_run_cleanups();\n");
    printf("    longjmp(EXC_STACK->env, 1);\n");
    printf("}\n\n");

    // Try/catch macros
    printf("// Try/catch macros\n");
    printf("#define TRY \\\n");
    printf("    do { \\\n");
    printf("        ExcFrame* _exc_frame = exc_push(); \\\n");
    printf("        if (setjmp(_exc_frame->env) == 0) {\n\n");

    printf("#define CATCH(exc_var) \\\n");
    printf("            exc_pop(); \\\n");
    printf("        } else { \\\n");
    printf("            Exception exc_var = EXC_STACK->exc; \\\n");
    printf("            exc_pop();\n\n");

    printf("#define END_TRY \\\n");
    printf("        } \\\n");
    printf("    } while(0)\n\n");

    // Allocate with auto-registration
    printf("// Allocate with automatic exception cleanup registration\n");
    printf("Obj* mk_int_exc(long val) {\n");
    printf("    Obj* obj = mk_int(val);\n");
    printf("    exc_register_cleanup(obj);\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("Obj* mk_pair_exc(Obj* a, Obj* b) {\n");
    printf("    Obj* obj = mk_pair(a, b);\n");
    printf("    exc_register_cleanup(obj);\n");
    printf("    return obj;\n");
    printf("}\n\n");

    // Safe free with unregistration
    printf("// Free with automatic unregistration\n");
    printf("void free_exc(Obj* obj) {\n");
    printf("    exc_unregister_cleanup(obj);\n");
    printf("    dec_ref(obj);\n");
    printf("}\n\n");
}

// Generate cleanup code for landing pad
void gen_landing_pad_code(LandingPad* pad) {
    if (!pad) return;

    printf("landing_pad_%d:\n", pad->id);

    CleanupAction* action = pad->cleanups;
    while (action) {
        printf("    %s(%s);\n", action->cleanup_fn, action->var_name);
        action = action->next;
    }

    printf("    // Resume unwinding or return to handler\n");
}
