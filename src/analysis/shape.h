#ifndef PURPLE_SHAPE_H
#define PURPLE_SHAPE_H

#include "../types.h"

// -- Phase 2: Shape Analysis (Ghiya-Hendren) --
// Classifies pointers as TREE/DAG/CYCLIC at compile time

typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_TREE,      // No sharing, no cycles - pure ASAP direct free
    SHAPE_DAG,       // Sharing but no cycles - refcount without cycle check
    SHAPE_CYCLIC     // May have cycles - needs SCC RC or deferred
} Shape;

typedef struct ShapeInfo {
    char* var_name;
    Shape shape;
    int confidence;      // 0-100, how certain we are
    int alias_group;     // Variables in same group may alias
    struct ShapeInfo* next;
} ShapeInfo;

typedef struct ShapeContext {
    ShapeInfo* shapes;
    int changed;         // For fixpoint iteration
    int next_alias_group;
    Shape result_shape;  // Shape of last expression result
} ShapeContext;

// Shape lattice operations
Shape shape_join(Shape a, Shape b);
const char* shape_to_string(Shape s);

// Context management
ShapeContext* mk_shape_context(void);
ShapeInfo* find_shape(ShapeContext* ctx, const char* name);
void add_shape(ShapeContext* ctx, const char* name, Shape shape);
Shape lookup_shape(ShapeContext* ctx, Value* expr);

// Alias analysis
int may_alias(ShapeContext* ctx, Value* a, Value* b);

// Shape analysis
void analyze_shapes_expr(Value* expr, ShapeContext* ctx);

// Get shape-based free strategy
const char* shape_free_strategy(Shape s);

#endif // PURPLE_SHAPE_H
