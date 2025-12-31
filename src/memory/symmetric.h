/*
 * Symmetric Reference Counting
 *
 * Key insight: Treat scope/stack frame as an object that participates in
 * the ownership graph. Each reference is bidirectional.
 *
 * This allows O(1) deterministic cycle collection without global GC:
 * - External refs: From live scopes/roots
 * - Internal refs: Within the object graph
 * - When external_rc drops to 0, the object (or cycle) is orphaned garbage
 */

#ifndef SYMMETRIC_H
#define SYMMETRIC_H

#include <stdlib.h>

/* Forward declarations */
typedef struct SymObj SymObj;
typedef struct SymScope SymScope;

/* Reference type */
typedef enum {
    REF_EXTERNAL = 0,  /* Reference from a scope/root */
    REF_INTERNAL = 1   /* Reference from another object */
} RefType;

/* Symmetric object with dual reference counts */
struct SymObj {
    int external_rc;        /* References from live scopes */
    int internal_rc;        /* References from other objects */
    SymObj** refs;          /* Objects this object references */
    int ref_count;          /* Number of outgoing references */
    int ref_capacity;       /* Capacity of refs array */
    void* data;             /* Actual data payload */
    int freed;              /* Mark to prevent double-free */
    void (*destructor)(void*);  /* Optional destructor for data */
};

/* Scope that owns objects */
struct SymScope {
    SymObj** owned;         /* Objects owned by this scope */
    int owned_count;
    int owned_capacity;
    SymScope* parent;       /* Parent scope (for nesting) */
};

/* Symmetric RC context */
typedef struct {
    SymScope* global_scope;
    SymScope** scope_stack;
    int stack_size;
    int stack_capacity;
    /* Statistics */
    int objects_created;
    int objects_freed;
    int cycles_collected;
} SymContext;

/* Object operations */
SymObj* sym_obj_new(void* data, void (*destructor)(void*));
void sym_obj_add_ref(SymObj* obj, SymObj* target);

/* Scope operations */
SymScope* sym_scope_new(SymScope* parent);
void sym_scope_own(SymScope* scope, SymObj* obj);
void sym_scope_release(SymScope* scope);
void sym_scope_free(SymScope* scope);

/* Reference counting */
void sym_inc_external(SymObj* obj);
void sym_dec_external(SymObj* obj);
void sym_inc_internal(SymObj* from, SymObj* to);
void sym_dec_internal(SymObj* obj);

/* Context operations */
SymContext* sym_context_new(void);
void sym_context_free(SymContext* ctx);
SymScope* sym_current_scope(SymContext* ctx);
SymScope* sym_enter_scope(SymContext* ctx);
void sym_exit_scope(SymContext* ctx);
SymObj* sym_alloc(SymContext* ctx, void* data, void (*destructor)(void*));
void sym_link(SymContext* ctx, SymObj* from, SymObj* to);

/* Utility */
int sym_is_orphaned(SymObj* obj);
int sym_total_rc(SymObj* obj);

#endif /* SYMMETRIC_H */
