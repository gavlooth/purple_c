/*
 * RC Optimization Module (Lobster-style)
 *
 * Implements compile-time reference counting optimization:
 * - Alias tracking: When two variables point to the same object
 * - Uniqueness analysis: Prove when an object has exactly one reference
 * - Borrow tracking: Parameters and temporary references without ownership
 *
 * Based on research from:
 * - Lobster language compile-time RC (95% RC ops eliminated)
 * - ASAP memory management
 */

#ifndef RCOPT_H
#define RCOPT_H

#include "../types.h"

/* RC Optimization types */
typedef enum {
    RC_OPT_NONE = 0,       /* No optimization - use standard RC */
    RC_OPT_ELIDE_INC,      /* Skip inc_ref (borrowed or already counted) */
    RC_OPT_ELIDE_DEC,      /* Skip dec_ref (another alias will handle it) */
    RC_OPT_DIRECT_FREE,    /* Use free() directly (proven unique) */
    RC_OPT_BATCHED_FREE,   /* Batch multiple frees together */
    RC_OPT_ELIDE_ALL       /* Eliminate all RC ops (unique + owned) */
} RCOptimization;

/* RC optimization info for a variable */
typedef struct RCOptInfo {
    char* var_name;
    int is_unique;         /* True if provably the only reference */
    int is_borrowed;       /* True if borrowed (no ownership transfer) */
    int defined_at;        /* Program point where defined */
    int last_used_at;      /* Program point of last use */
    char* alias_of;        /* If this is an alias, name of original */
    char** aliases;        /* List of other variables that alias this one */
    int alias_count;
    int alias_capacity;
    struct RCOptInfo* next;
} RCOptInfo;

/* RC optimization context */
typedef struct RCOptContext {
    RCOptInfo* vars;       /* Linked list of variable info */
    int current_point;     /* Current program point counter */
    int eliminated;        /* Count of eliminated RC operations */
} RCOptContext;

/* Create/destroy context */
RCOptContext* mk_rcopt_context(void);
void free_rcopt_context(RCOptContext* ctx);

/* Define variables */
RCOptInfo* rcopt_define_var(RCOptContext* ctx, const char* name);
RCOptInfo* rcopt_define_alias(RCOptContext* ctx, const char* name, const char* alias_of);
RCOptInfo* rcopt_define_borrowed(RCOptContext* ctx, const char* name);

/* Mark variable usage */
void rcopt_mark_used(RCOptContext* ctx, const char* name);

/* Query optimizations */
RCOptimization rcopt_get_inc_ref(RCOptContext* ctx, const char* name);
RCOptimization rcopt_get_dec_ref(RCOptContext* ctx, const char* name);

/* Get optimized free function */
const char* rcopt_get_free_function(RCOptContext* ctx, const char* name, int shape);

/* Analyze expression for RC optimization */
void rcopt_analyze_expr(RCOptContext* ctx, Value* expr);

/* Get statistics */
void rcopt_get_stats(RCOptContext* ctx, int* total, int* eliminated);

/* Utility */
RCOptInfo* rcopt_find_var(RCOptContext* ctx, const char* name);
const char* rcopt_string(RCOptimization opt);

#endif /* RCOPT_H */
