# ASAP Memory Management - Comprehensive Implementation Plan

> **Goal**: Implement all ASAP optimizations with NO garbage collection, NO stop-the-world,
> fully deterministic memory management, and NO language restrictions.

---

## Primary Strategy: ASAP + ISMM 2024

### Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    COMPILE TIME                             │
├─────────────────────────────────────────────────────────────┤
│  Shape Analysis ──→ Classify as TREE / DAG / CYCLIC         │
│  Escape Analysis ──→ Stack vs Heap allocation               │
│  Freeze Detection ──→ Identify immutable cyclic structures  │
│  Drop-Guided Reuse ──→ Pair free/alloc for in-place reuse   │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    PRIMARY STRATEGY                         │
├─────────────────────────────────────────────────────────────┤
│  Acyclic (TREE/DAG) ──→ ASAP compile-time free insertion    │
│  Cyclic + Frozen    ──→ SCC-based RC (ISMM 2024)            │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    FALLBACK (edge cases)                    │
├─────────────────────────────────────────────────────────────┤
│  Mutable cycles that never freeze                           │
│  ──→ Deferred RC with bounded processing at safe points     │
└─────────────────────────────────────────────────────────────┘
```

### Data Classification & Handling

| Data Type | Strategy | Pause | Proven |
|-----------|----------|-------|--------|
| TREE (no sharing) | ASAP `free_tree()` | Zero | Yes |
| DAG (sharing, no cycles) | ASAP `dec_ref()` | Zero | Yes |
| Cyclic + Frozen | SCC-level RC (ISMM 2024) | Zero | Yes |
| Cyclic + Mutable | Deferred list + bounded processing | Zero | Yes |

### Key Papers

| Paper | Role |
|-------|------|
| [ASAP (2020)](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf) | Acyclic data handling |
| [ISMM 2024 - Deeply Immutable Cycles](https://dl.acm.org/doi/10.1145/3652024.3665507) | Cyclic data handling |
| [Drop-Guided Reuse (2022)](https://dl.acm.org/doi/10.1145/3547634) | Memory reuse optimization |

---

## Table of Contents

### Core (Completed)
1. [Phase 1: Core Infrastructure](#phase-1-core-infrastructure-completed) ✅
2. [Phase 2: Shape Analysis (ASAP)](#phase-2-shape-analysis-completed) ✅
3. [Phase 3: Typed Reference Fields](#phase-3-typed-reference-fields-auto-weak-completed) ✅
4. [Phase 4: Drop-Guided Reuse](#phase-4-perceus-reuse-analysis-completed) ✅
5. [Phase 5: Non-Lexical Lifetimes](#phase-6-non-lexical-lifetimes-completed) ✅

### Primary Strategy (Next)
6. [Phase 6: Freeze Detection & SCC-based RC (ISMM 2024)](#phase-6-freeze-detection--scc-based-rc-ismm-2024)
7. [Phase 7: Deferred RC Fallback](#phase-7-deferred-rc-fallback)

### Optimizations
8. [Phase 8: Destination-Passing Style](#phase-5-destination-passing-style)
9. [Phase 9: Exception Handling](#phase-8-exception-handling)
10. [Phase 10: Concurrency Support](#phase-9-concurrency-support)

---

## Phase 1: Core Infrastructure (COMPLETED)

**Status**: ✅ Done

- [x] VarUsage and AnalysisContext data structures
- [x] Liveness analysis
- [x] Escape analysis (ESCAPE_NONE, ESCAPE_ARG, ESCAPE_GLOBAL)
- [x] Capture tracking for closures
- [x] Dynamic free list
- [x] Multi-binding let support
- [x] Type-aware scanners

**Reference**: `main.c` lines 81-547

---

## Phase 2: Shape Analysis (COMPLETED)

**Status**: ✅ Done

**Goal**: Classify each pointer as Tree/DAG/Cyclic at compile time.

**Reference**: [Ghiya & Hendren - Shape Analysis](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-A-shape-for-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040)

### Tasks

- [x] **2.1** Define shape enum and data structures
- [x] **2.2** Implement intraprocedural shape analysis
- [x] **2.3** Implement interprocedural shape propagation
- [x] **2.4** Integrate with code generation
- [x] **2.5** Add tests for shape analysis

### 2.1 Define Shape Data Structures

```c
// Add to main.c after AnalysisContext

typedef enum {
    SHAPE_UNKNOWN = 0,
    SHAPE_TREE,      // No sharing, no cycles - pure ASAP
    SHAPE_DAG,       // Sharing but no cycles - refcount only
    SHAPE_CYCLIC     // May have cycles - needs special handling
} Shape;

typedef struct ShapeInfo {
    char* var_name;
    Shape shape;
    int confidence;      // 0-100, how certain we are
    struct ShapeInfo* next;
} ShapeInfo;

typedef struct ShapeContext {
    ShapeInfo* shapes;
    int changed;         // For fixpoint iteration
} ShapeContext;

// Shape lattice: TREE < DAG < CYCLIC (join = max)
Shape shape_join(Shape a, Shape b) {
    if (a == SHAPE_CYCLIC || b == SHAPE_CYCLIC) return SHAPE_CYCLIC;
    if (a == SHAPE_DAG || b == SHAPE_DAG) return SHAPE_DAG;
    return SHAPE_TREE;
}
```

### 2.2 Intraprocedural Shape Analysis

```c
// Analyze a single function/expression for shapes

ShapeContext* analyze_shapes(Value* expr, ShapeContext* ctx) {
    if (!expr || is_nil(expr)) return ctx;

    switch (expr->tag) {
        case T_CELL: {
            Value* op = car(expr);
            Value* args = cdr(expr);

            if (op->tag == T_SYM) {
                // CONS creates tree structure (unless aliased)
                if (strcmp(op->s, "cons") == 0) {
                    // Result is TREE if both args are TREE and not aliased
                    Value* car_arg = car(args);
                    Value* cdr_arg = car(cdr(args));
                    Shape car_shape = lookup_shape(ctx, car_arg);
                    Shape cdr_shape = lookup_shape(ctx, cdr_arg);

                    if (car_shape == SHAPE_TREE && cdr_shape == SHAPE_TREE) {
                        // Check for aliasing
                        if (!may_alias(car_arg, cdr_arg)) {
                            return set_result_shape(ctx, SHAPE_TREE);
                        }
                    }
                    return set_result_shape(ctx, SHAPE_DAG);
                }

                // LET binding
                if (strcmp(op->s, "let") == 0) {
                    Value* bindings = car(args);
                    Value* body = car(cdr(args));

                    // Analyze each binding
                    while (!is_nil(bindings)) {
                        Value* bind = car(bindings);
                        Value* sym = car(bind);
                        Value* val_expr = car(cdr(bind));

                        // Recursively analyze value expression
                        ShapeContext* val_ctx = analyze_shapes(val_expr, ctx);
                        Shape val_shape = get_result_shape(val_ctx);

                        // Record shape for this variable
                        add_shape(ctx, sym->s, val_shape);

                        bindings = cdr(bindings);
                    }

                    // Analyze body with updated context
                    return analyze_shapes(body, ctx);
                }

                // SET! can create cycles
                if (strcmp(op->s, "set!") == 0) {
                    Value* target = car(args);
                    Value* val = car(cdr(args));

                    // If setting a field that could point back, mark as CYCLIC
                    if (could_create_cycle(target, val, ctx)) {
                        mark_shape(ctx, target->s, SHAPE_CYCLIC);
                    }
                    return ctx;
                }
            }

            // Default: analyze all subexpressions
            analyze_shapes(op, ctx);
            while (!is_nil(args)) {
                analyze_shapes(car(args), ctx);
                args = cdr(args);
            }
            break;
        }

        case T_SYM:
            // Variable reference - lookup its shape
            return ctx;

        default:
            // Literals are always TREE
            return set_result_shape(ctx, SHAPE_TREE);
    }

    return ctx;
}
```

### 2.3 Interprocedural Shape Propagation

```c
// Function shape summary
typedef struct FunctionShapeSummary {
    char* name;
    Shape* param_shapes;      // Input shapes
    int param_count;
    Shape return_shape;       // Output shape
    int modifies_params;      // Bitmask of which params are modified
} FunctionShapeSummary;

// Global function summaries
FunctionShapeSummary* FUNC_SUMMARIES = NULL;

// Compute function summary
FunctionShapeSummary* summarize_function(Value* func_def) {
    // (define (name params...) body)
    Value* name = car(cdr(func_def));
    Value* params = cdr(cdr(func_def));
    Value* body = car(cdr(cdr(cdr(func_def))));

    FunctionShapeSummary* summary = malloc(sizeof(FunctionShapeSummary));
    summary->name = strdup(name->s);

    // Initialize params as TREE (most optimistic)
    int count = list_length(car(params));
    summary->param_shapes = malloc(count * sizeof(Shape));
    for (int i = 0; i < count; i++) {
        summary->param_shapes[i] = SHAPE_TREE;
    }
    summary->param_count = count;

    // Analyze body to determine return shape
    ShapeContext* ctx = mk_shape_context();
    // Add params to context
    Value* p = car(params);
    int i = 0;
    while (!is_nil(p)) {
        add_shape(ctx, car(p)->s, SHAPE_TREE);
        p = cdr(p);
        i++;
    }

    ctx = analyze_shapes(body, ctx);
    summary->return_shape = get_result_shape(ctx);

    return summary;
}

// Fixpoint iteration for whole program
void analyze_program_shapes(Value* program) {
    int changed = 1;
    int iterations = 0;
    int max_iterations = 100;

    while (changed && iterations < max_iterations) {
        changed = 0;
        iterations++;

        // Analyze each function definition
        Value* defs = program;
        while (!is_nil(defs)) {
            Value* def = car(defs);
            if (is_function_def(def)) {
                FunctionShapeSummary* old = find_summary(car(cdr(def))->s);
                FunctionShapeSummary* new = summarize_function(def);

                if (!shapes_equal(old, new)) {
                    changed = 1;
                    update_summary(new);
                }
            }
            defs = cdr(defs);
        }
    }
}
```

### 2.4 Integrate with Code Generation

```c
// In h_let_default, use shape info for memory strategy

Value* h_let_default_with_shapes(Value* exp, Value* menv, ShapeContext* shapes) {
    // ... existing code ...

    Shape var_shape = lookup_shape(shapes, sym->s);

    switch (var_shape) {
        case SHAPE_TREE:
            // Pure ASAP - direct free, no refcounting
            sprintf(free_code, "free_tree(%s);", sym->s);
            break;

        case SHAPE_DAG:
            // Refcount without cycle detection
            sprintf(free_code, "dec_ref_nocycle(%s);", sym->s);
            break;

        case SHAPE_CYCLIC:
            // Use arena or local cycle detection
            if (escapes_scope(sym, body)) {
                sprintf(free_code, "dec_ref_cyclic(%s);", sym->s);
            } else {
                // Will be freed with arena
                sprintf(free_code, "// %s freed with arena", sym->s);
            }
            break;
    }

    // ... rest of code generation ...
}
```

### 2.5 Tests for Shape Analysis

```bash
# Add to tests.sh

# Shape Analysis Tests
run_test "Shape-Tree" \
    "(let ((x (cons (lift 1) (lift 2)))) x)" \
    "free_tree"

run_test "Shape-DAG" \
    "(let ((shared (lift 1))) (cons shared shared))" \
    "dec_ref"

run_test "Shape-Cyclic-Detect" \
    "(let ((node (make-node))) (set-next! node node))" \
    "cyclic"
```

---

## Phase 3: Typed Reference Fields (Auto-Weak) (COMPLETED)

**Status**: ✅ Done

**Goal**: Automatically detect back-edge fields and make them weak references.

**Reference**: [Typed Reference Counting](https://www.sciencedirect.com/science/article/abs/pii/S1477842411000285)

### Tasks

- [x] **3.1** Define type/struct registry
- [x] **3.2** Build ownership graph from type definitions
- [x] **3.3** Detect back-edge fields
- [x] **3.4** Generate weak reference handling code
- [x] **3.5** Add tests

### 3.1 Type/Struct Registry

```c
typedef enum {
    FIELD_STRONG,    // Normal strong reference
    FIELD_WEAK,      // Weak reference (doesn't prevent deallocation)
    FIELD_UNTRACED   // Non-pointer field (int, float, etc.)
} FieldStrength;

typedef struct FieldDef {
    char* name;
    char* type_name;      // Type this field points to
    FieldStrength strength;
    int is_pointer;
    struct FieldDef* next;
} FieldDef;

typedef struct TypeDef {
    char* name;
    FieldDef* fields;
    int is_recursive;     // Type contains pointer to itself
    struct TypeDef* next;
} TypeDef;

TypeDef* TYPE_REGISTRY = NULL;

// Register a new type
void register_type(const char* name) {
    TypeDef* t = malloc(sizeof(TypeDef));
    t->name = strdup(name);
    t->fields = NULL;
    t->is_recursive = 0;
    t->next = TYPE_REGISTRY;
    TYPE_REGISTRY = t;
}

// Add field to type
void add_field(const char* type_name, const char* field_name,
               const char* field_type, int is_pointer) {
    TypeDef* t = find_type(type_name);
    if (!t) return;

    FieldDef* f = malloc(sizeof(FieldDef));
    f->name = strdup(field_name);
    f->type_name = strdup(field_type);
    f->is_pointer = is_pointer;
    f->strength = FIELD_STRONG;  // Default, will be analyzed
    f->next = t->fields;
    t->fields = f;

    // Check if recursive
    if (is_pointer && strcmp(field_type, type_name) == 0) {
        t->is_recursive = 1;
    }
}
```

### 3.2 Build Ownership Graph

```c
typedef struct OwnershipEdge {
    char* from_type;
    char* field_name;
    char* to_type;
    int is_back_edge;     // Determined by analysis
    struct OwnershipEdge* next;
} OwnershipEdge;

OwnershipEdge* OWNERSHIP_GRAPH = NULL;

// Build graph from type definitions
void build_ownership_graph() {
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        FieldDef* f = t->fields;
        while (f) {
            if (f->is_pointer) {
                OwnershipEdge* e = malloc(sizeof(OwnershipEdge));
                e->from_type = strdup(t->name);
                e->field_name = strdup(f->name);
                e->to_type = strdup(f->type_name);
                e->is_back_edge = 0;  // Will be computed
                e->next = OWNERSHIP_GRAPH;
                OWNERSHIP_GRAPH = e;
            }
            f = f->next;
        }
        t = t->next;
    }
}
```

### 3.3 Detect Back-Edge Fields

```c
// Detect cycles in ownership graph using DFS
typedef struct VisitState {
    char* type_name;
    int color;  // 0=white, 1=gray, 2=black
    struct VisitState* next;
} VisitState;

int detect_back_edges_dfs(const char* type_name, VisitState* visited,
                          const char* path[], int path_len) {
    // Check if already in current path (gray) -> back edge!
    VisitState* v = find_visit_state(visited, type_name);
    if (v && v->color == 1) {
        return 1;  // Found cycle
    }
    if (v && v->color == 2) {
        return 0;  // Already fully explored
    }

    // Mark as being visited (gray)
    v = add_visit_state(&visited, type_name, 1);

    // Add to path
    path[path_len] = type_name;
    path_len++;

    // Visit all outgoing edges
    OwnershipEdge* e = OWNERSHIP_GRAPH;
    while (e) {
        if (strcmp(e->from_type, type_name) == 0) {
            // Check if this edge goes to something in our path
            for (int i = 0; i < path_len; i++) {
                if (strcmp(path[i], e->to_type) == 0) {
                    // This is a back edge!
                    e->is_back_edge = 1;
                    mark_field_weak(e->from_type, e->field_name);
                }
            }

            // Recurse
            detect_back_edges_dfs(e->to_type, visited, path, path_len);
        }
        e = e->next;
    }

    // Mark as fully explored (black)
    v->color = 2;
    return 0;
}

void analyze_back_edges() {
    const char* path[256];
    VisitState* visited = NULL;

    // Start DFS from each type
    TypeDef* t = TYPE_REGISTRY;
    while (t) {
        detect_back_edges_dfs(t->name, visited, path, 0);
        t = t->next;
    }
}

void mark_field_weak(const char* type_name, const char* field_name) {
    TypeDef* t = find_type(type_name);
    if (!t) return;

    FieldDef* f = t->fields;
    while (f) {
        if (strcmp(f->name, field_name) == 0) {
            f->strength = FIELD_WEAK;
            printf("// AUTO-WEAK: %s.%s\n", type_name, field_name);
            return;
        }
        f = f->next;
    }
}
```

### 3.4 Generate Weak Reference Code

```c
// Generate struct definition with weak refs
void gen_struct_def(TypeDef* t) {
    printf("typedef struct %s {\n", t->name);
    printf("    int _rc;           // Reference count\n");
    printf("    int _weak_rc;      // Weak reference count\n");

    FieldDef* f = t->fields;
    while (f) {
        if (f->is_pointer) {
            if (f->strength == FIELD_WEAK) {
                printf("    struct %s* %s;  // WEAK\n", f->type_name, f->name);
            } else {
                printf("    struct %s* %s;  // STRONG\n", f->type_name, f->name);
            }
        } else {
            printf("    %s %s;\n", f->type_name, f->name);
        }
        f = f->next;
    }

    printf("} %s;\n\n", t->name);
}

// Generate release function that handles weak refs
void gen_release_func(TypeDef* t) {
    printf("void release_%s(%s* obj) {\n", t->name, t->name);
    printf("    if (!obj) return;\n");
    printf("    obj->_rc--;\n");
    printf("    if (obj->_rc == 0) {\n");

    // Release strong children only
    FieldDef* f = t->fields;
    while (f) {
        if (f->is_pointer && f->strength == FIELD_STRONG) {
            printf("        release_%s(obj->%s);\n", f->type_name, f->name);
        }
        f = f->next;
    }

    printf("        // Check weak refs before freeing\n");
    printf("        if (obj->_weak_rc == 0) {\n");
    printf("            free(obj);\n");
    printf("        } else {\n");
    printf("            obj->_rc = -1;  // Mark as dead\n");
    printf("        }\n");
    printf("    }\n");
    printf("}\n\n");
}

// Weak reference accessor
void gen_weak_accessor(TypeDef* t, FieldDef* f) {
    printf("// Safe weak ref access for %s.%s\n", t->name, f->name);
    printf("%s* get_%s_%s(%s* obj) {\n", f->type_name, t->name, f->name, t->name);
    printf("    if (!obj || !obj->%s) return NULL;\n", f->name);
    printf("    if (obj->%s->_rc <= 0) {\n", f->name);
    printf("        // Referent is dead, clear weak ref\n");
    printf("        obj->%s->_weak_rc--;\n", f->name);
    printf("        obj->%s = NULL;\n", f->name);
    printf("        return NULL;\n");
    printf("    }\n");
    printf("    return obj->%s;\n", f->name);
    printf("}\n\n");
}
```

### 3.5 Tests

```bash
# Add to tests.sh

# Auto-weak detection tests
run_test "AutoWeak-DoublyLinked" \
    "(defstruct Node (value next prev))" \
    "// AUTO-WEAK: Node.prev"

run_test "AutoWeak-TreeParent" \
    "(defstruct TreeNode (value left right parent))" \
    "// AUTO-WEAK: TreeNode.parent"

run_test "AutoWeak-NoFalsePositive" \
    "(defstruct List (value next))" \
    "STRONG"
```

---

## Phase 4: Perceus Reuse Analysis (COMPLETED)

**Status**: ✅ Done

**Goal**: Pair `free` with subsequent `alloc` of same size for in-place reuse.

**Reference**: [Perceus: Garbage Free Reference Counting](https://dl.acm.org/doi/10.1145/3453483.3454032)

### Tasks

- [x] **4.1** Track object sizes at allocation sites
- [x] **4.2** Match free/alloc pairs
- [x] **4.3** Generate reuse code
- [x] **4.4** Handle conditional reuse
- [x] **4.5** Add tests

### 4.1 Track Object Sizes

```c
typedef struct AllocSite {
    int id;
    char* var_name;
    int size;             // Size in bytes
    int line_number;
    struct AllocSite* next;
} AllocSite;

typedef struct FreeSite {
    int id;
    char* var_name;
    int size;
    int line_number;
    int matched_alloc_id;  // -1 if not matched
    struct FreeSite* next;
} FreeSite;

typedef struct ReuseContext {
    AllocSite* allocs;
    FreeSite* frees;
    int next_id;
} ReuseContext;

// Record an allocation
void record_alloc(ReuseContext* ctx, const char* var, int size, int line) {
    AllocSite* a = malloc(sizeof(AllocSite));
    a->id = ctx->next_id++;
    a->var_name = strdup(var);
    a->size = size;
    a->line_number = line;
    a->next = ctx->allocs;
    ctx->allocs = a;
}

// Record a free
void record_free(ReuseContext* ctx, const char* var, int size, int line) {
    FreeSite* f = malloc(sizeof(FreeSite));
    f->id = ctx->next_id++;
    f->var_name = strdup(var);
    f->size = size;
    f->line_number = line;
    f->matched_alloc_id = -1;
    f->next = ctx->frees;
    ctx->frees = f;
}
```

### 4.2 Match Free/Alloc Pairs

```c
typedef struct ReusePair {
    FreeSite* free_site;
    AllocSite* alloc_site;
    char* reuse_var;      // Variable holding potentially reusable memory
    struct ReusePair* next;
} ReusePair;

// Find reuse opportunities
ReusePair* find_reuse_pairs(ReuseContext* ctx) {
    ReusePair* pairs = NULL;

    // For each free, find subsequent alloc of same size
    FreeSite* f = ctx->frees;
    while (f) {
        AllocSite* a = ctx->allocs;
        while (a) {
            // Must be: free before alloc, same size, not already matched
            if (f->line_number < a->line_number &&
                f->size == a->size &&
                f->matched_alloc_id == -1) {

                // Check no intervening use of freed variable
                if (!used_between(f->var_name, f->line_number, a->line_number)) {
                    // Found a match!
                    ReusePair* p = malloc(sizeof(ReusePair));
                    p->free_site = f;
                    p->alloc_site = a;
                    p->reuse_var = strdup(f->var_name);
                    p->next = pairs;
                    pairs = p;

                    f->matched_alloc_id = a->id;
                    break;  // Move to next free
                }
            }
            a = a->next;
        }
        f = f->next;
    }

    return pairs;
}
```

### 4.3 Generate Reuse Code

```c
// Generate reuse-aware allocation
void gen_reuse_alloc(ReusePair* pair) {
    printf("// PERCEUS REUSE: %s -> %s\n",
           pair->free_site->var_name,
           pair->alloc_site->var_name);

    printf("Obj* %s;\n", pair->alloc_site->var_name);
    printf("if (%s != NULL && %s->_rc == 1) {\n",
           pair->reuse_var, pair->reuse_var);
    printf("    // Reuse in place - FBIP optimization\n");
    printf("    %s = %s;\n", pair->alloc_site->var_name, pair->reuse_var);
    printf("    %s = NULL;  // Prevent double-free\n", pair->reuse_var);
    printf("} else {\n");
    printf("    // Cannot reuse, allocate fresh\n");
    printf("    if (%s) dec_ref(%s);\n", pair->reuse_var, pair->reuse_var);
    printf("    %s = malloc(sizeof(Obj));\n", pair->alloc_site->var_name);
    printf("}\n");
}

// Runtime reuse function
void gen_reuse_runtime() {
    printf("// Perceus reuse helper\n");
    printf("Obj* try_reuse(Obj* old, size_t size) {\n");
    printf("    if (old && old->_rc == 1) {\n");
    printf("        // Unique reference - can reuse\n");
    printf("        return old;\n");
    printf("    }\n");
    printf("    if (old) dec_ref(old);\n");
    printf("    return malloc(size);\n");
    printf("}\n\n");

    printf("// Reuse-aware constructors\n");
    printf("Obj* reuse_as_int(Obj* old, long value) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    obj->tag = T_INT;\n");
    printf("    obj->i = value;\n");
    printf("    obj->_rc = 1;\n");
    printf("    return obj;\n");
    printf("}\n\n");

    printf("Obj* reuse_as_pair(Obj* old, Obj* a, Obj* b) {\n");
    printf("    Obj* obj = try_reuse(old, sizeof(Obj));\n");
    printf("    obj->tag = T_PAIR;\n");
    printf("    obj->a = a;\n");
    printf("    obj->b = b;\n");
    printf("    obj->_rc = 1;\n");
    printf("    return obj;\n");
    printf("}\n\n");
}
```

### 4.4 Handle Conditional Reuse

```c
// For conditional paths, generate runtime check
void gen_conditional_reuse(const char* cond, ReusePair* if_reuse, ReusePair* else_reuse) {
    printf("Obj* result;\n");
    printf("if (%s) {\n", cond);
    if (if_reuse) {
        printf("    result = reuse_as_int(%s, new_value);\n", if_reuse->reuse_var);
    } else {
        printf("    result = mk_int(new_value);\n");
    }
    printf("} else {\n");
    if (else_reuse) {
        printf("    result = reuse_as_int(%s, other_value);\n", else_reuse->reuse_var);
    } else {
        printf("    result = mk_int(other_value);\n");
    }
    printf("}\n");
}
```

### 4.5 Tests

```bash
# Reuse tests
run_test "Perceus-SimpleReuse" \
    "(let ((x (lift 1))) (let ((y (+ x 1))) y))" \
    "try_reuse"

run_test "Perceus-FBIP" \
    "(define (inc-all lst) (map (lambda (x) (+ x 1)) lst))" \
    "reuse_as"
```

---

## Phase 5: Destination-Passing Style

**Goal**: Pass destination pointer to avoid intermediate allocations.

**Reference**: [Destination-Passing Style](https://dl.acm.org/doi/10.1145/3122948.3122949)

### Tasks

- [ ] **5.1** Identify DPS candidates (functions returning fresh allocations)
- [ ] **5.2** Transform function signatures
- [ ] **5.3** Transform call sites
- [ ] **5.4** Stack-allocate destinations where possible
- [ ] **5.5** Add tests

### 5.1 Identify DPS Candidates

```c
typedef struct DPSCandidate {
    char* func_name;
    int returns_fresh;        // Always returns newly allocated
    int result_size;          // Size of result
    int can_transform;        // Safe to transform?
    struct DPSCandidate* next;
} DPSCandidate;

// Analyze if function always returns fresh allocation
int returns_fresh_alloc(Value* func_body) {
    // Check all return paths
    // Return true if ALL paths return mk_*, cons, etc.

    if (!func_body) return 0;

    if (func_body->tag == T_CELL) {
        Value* op = car(func_body);
        if (op->tag == T_SYM) {
            // Direct allocation
            if (strcmp(op->s, "cons") == 0 ||
                strcmp(op->s, "mk-int") == 0 ||
                strcmp(op->s, "mk-pair") == 0) {
                return 1;
            }

            // If expression - check both branches
            if (strcmp(op->s, "if") == 0) {
                Value* then_branch = car(cdr(cdr(func_body)));
                Value* else_branch = car(cdr(cdr(cdr(func_body))));
                return returns_fresh_alloc(then_branch) &&
                       returns_fresh_alloc(else_branch);
            }

            // Let - check body
            if (strcmp(op->s, "let") == 0) {
                Value* body = car(cdr(cdr(func_body)));
                return returns_fresh_alloc(body);
            }
        }
    }

    return 0;
}

DPSCandidate* find_dps_candidates(Value* program) {
    DPSCandidate* candidates = NULL;

    Value* defs = program;
    while (!is_nil(defs)) {
        Value* def = car(defs);
        if (is_function_def(def)) {
            char* name = get_func_name(def);
            Value* body = get_func_body(def);

            if (returns_fresh_alloc(body)) {
                DPSCandidate* c = malloc(sizeof(DPSCandidate));
                c->func_name = strdup(name);
                c->returns_fresh = 1;
                c->result_size = estimate_result_size(body);
                c->can_transform = !has_side_effects(body);
                c->next = candidates;
                candidates = c;
            }
        }
        defs = cdr(defs);
    }

    return candidates;
}
```

### 5.2 Transform Function Signatures

```c
// Original: List* map(Fn* f, List* xs)
// DPS:      void map_dps(List* dest, Fn* f, List* xs)

void gen_dps_function(DPSCandidate* func, Value* body) {
    printf("// DPS-transformed version of %s\n", func->func_name);
    printf("void %s_dps(Obj* _dest, ", func->func_name);
    // ... print original params ...
    printf(") {\n");

    // Transform body to write to _dest instead of return
    gen_dps_body(body, "_dest");

    printf("}\n\n");

    // Keep original as wrapper
    printf("Obj* %s(", func->func_name);
    // ... print original params ...
    printf(") {\n");
    printf("    Obj* _result = malloc(sizeof(Obj));\n");
    printf("    %s_dps(_result, ", func->func_name);
    // ... print args ...
    printf(");\n");
    printf("    return _result;\n");
    printf("}\n\n");
}

void gen_dps_body(Value* body, const char* dest) {
    if (body->tag == T_CELL) {
        Value* op = car(body);
        if (op->tag == T_SYM) {
            // cons -> write to destination
            if (strcmp(op->s, "cons") == 0) {
                Value* a = car(cdr(body));
                Value* b = car(cdr(cdr(body)));
                printf("    %s->tag = T_PAIR;\n", dest);
                printf("    %s->a = ", dest); gen_expr(a); printf(";\n");
                printf("    %s->b = ", dest); gen_expr(b); printf(";\n");
                return;
            }

            // if -> transform both branches
            if (strcmp(op->s, "if") == 0) {
                Value* cond = car(cdr(body));
                Value* then_b = car(cdr(cdr(body)));
                Value* else_b = car(cdr(cdr(cdr(body))));

                printf("    if ("); gen_expr(cond); printf(") {\n");
                gen_dps_body(then_b, dest);
                printf("    } else {\n");
                gen_dps_body(else_b, dest);
                printf("    }\n");
                return;
            }
        }
    }

    // Default: evaluate and copy to dest
    printf("    *%s = *(", dest); gen_expr(body); printf(");\n");
}
```

### 5.3 Transform Call Sites

```c
// At call sites, stack-allocate destination when possible

void gen_dps_call(const char* func_name, Value* args, const char* result_var) {
    DPSCandidate* c = find_candidate(func_name);

    if (c && c->can_transform) {
        // Use DPS version with stack allocation
        printf("    Obj %s_storage;  // Stack allocated\n", result_var);
        printf("    Obj* %s = &%s_storage;\n", result_var, result_var);
        printf("    %s_dps(%s", func_name, result_var);
        // print args
        while (!is_nil(args)) {
            printf(", ");
            gen_expr(car(args));
            args = cdr(args);
        }
        printf(");\n");
    } else {
        // Use original version
        printf("    Obj* %s = %s(", result_var, func_name);
        // print args
        printf(");\n");
    }
}
```

### 5.4 & 5.5 Tests

```bash
run_test "DPS-Map" \
    "(define (map f xs) (if (null? xs) '() (cons (f (car xs)) (map f (cdr xs)))))" \
    "map_dps"

run_test "DPS-StackAlloc" \
    "(let ((result (map inc xs))) (sum result))" \
    "_storage"
```

---

## Phase 6: Non-Lexical Lifetimes (COMPLETED)

**Status**: ✅ Done

**Goal**: Free variables at earliest safe point, not just scope end.

**Reference**: [Rust Borrow Checker](https://rustc-dev-guide.rust-lang.org/borrow_check.html)

### Tasks

- [x] **6.1** Build Control Flow Graph (CFG)
- [x] **6.2** Compute liveness at each CFG node
- [x] **6.3** Find earliest free point per variable
- [x] **6.4** Generate path-specific frees
- [x] **6.5** Add tests

### 6.1 Build CFG

```c
typedef struct CFGNode {
    int id;
    Value* expr;              // AST node
    struct CFGNode** succs;   // Successor nodes
    int succ_count;
    struct CFGNode** preds;   // Predecessor nodes
    int pred_count;

    // Liveness info
    char** live_in;
    int live_in_count;
    char** live_out;
    int live_out_count;
} CFGNode;

typedef struct CFG {
    CFGNode* entry;
    CFGNode* exit;
    CFGNode** nodes;
    int node_count;
} CFG;

CFG* build_cfg(Value* expr) {
    CFG* cfg = malloc(sizeof(CFG));
    cfg->nodes = malloc(1000 * sizeof(CFGNode*));
    cfg->node_count = 0;

    cfg->entry = mk_cfg_node(cfg, NULL);
    cfg->exit = mk_cfg_node(cfg, NULL);

    build_cfg_recursive(cfg, expr, cfg->entry, cfg->exit);

    return cfg;
}

void build_cfg_recursive(CFG* cfg, Value* expr, CFGNode* pred, CFGNode* succ) {
    if (!expr || is_nil(expr)) {
        add_edge(pred, succ);
        return;
    }

    if (expr->tag == T_CELL) {
        Value* op = car(expr);

        if (op->tag == T_SYM && strcmp(op->s, "if") == 0) {
            // If creates branch in CFG
            Value* cond = car(cdr(expr));
            Value* then_br = car(cdr(cdr(expr)));
            Value* else_br = car(cdr(cdr(cdr(expr))));

            CFGNode* cond_node = mk_cfg_node(cfg, cond);
            CFGNode* then_entry = mk_cfg_node(cfg, NULL);
            CFGNode* else_entry = mk_cfg_node(cfg, NULL);
            CFGNode* merge = mk_cfg_node(cfg, NULL);

            add_edge(pred, cond_node);
            add_edge(cond_node, then_entry);  // true branch
            add_edge(cond_node, else_entry);  // false branch

            build_cfg_recursive(cfg, then_br, then_entry, merge);
            build_cfg_recursive(cfg, else_br, else_entry, merge);

            add_edge(merge, succ);
            return;
        }

        if (op->tag == T_SYM && strcmp(op->s, "let") == 0) {
            // Let is sequential
            Value* bindings = car(cdr(expr));
            Value* body = car(cdr(cdr(expr)));

            CFGNode* prev = pred;
            while (!is_nil(bindings)) {
                Value* bind = car(bindings);
                CFGNode* bind_node = mk_cfg_node(cfg, bind);
                add_edge(prev, bind_node);
                prev = bind_node;
                bindings = cdr(bindings);
            }

            build_cfg_recursive(cfg, body, prev, succ);
            return;
        }
    }

    // Simple expression
    CFGNode* node = mk_cfg_node(cfg, expr);
    add_edge(pred, node);
    add_edge(node, succ);
}
```

### 6.2 Compute Liveness

```c
// Dataflow analysis for liveness
void compute_liveness(CFG* cfg) {
    int changed = 1;

    while (changed) {
        changed = 0;

        // Iterate in reverse postorder
        for (int i = cfg->node_count - 1; i >= 0; i--) {
            CFGNode* node = cfg->nodes[i];

            // live_out = union of successors' live_in
            char** new_live_out = NULL;
            int new_count = 0;
            for (int j = 0; j < node->succ_count; j++) {
                merge_sets(&new_live_out, &new_count,
                          node->succs[j]->live_in,
                          node->succs[j]->live_in_count);
            }

            // live_in = use(node) ∪ (live_out - def(node))
            char** uses = get_uses(node->expr);
            char** defs = get_defs(node->expr);

            char** new_live_in = NULL;
            int new_in_count = 0;

            // Add uses
            merge_sets(&new_live_in, &new_in_count, uses, count_strings(uses));

            // Add live_out - defs
            for (int j = 0; j < new_count; j++) {
                if (!in_set(defs, new_live_out[j])) {
                    add_to_set(&new_live_in, &new_in_count, new_live_out[j]);
                }
            }

            // Check for change
            if (!sets_equal(node->live_in, node->live_in_count,
                           new_live_in, new_in_count)) {
                changed = 1;
                node->live_in = new_live_in;
                node->live_in_count = new_in_count;
            }

            node->live_out = new_live_out;
            node->live_out_count = new_count;
        }
    }
}
```

### 6.3 Find Earliest Free Point

```c
typedef struct FreePoint {
    char* var_name;
    CFGNode* node;          // Free after this node
    int is_conditional;     // Only free on some paths?
    struct FreePoint* next;
} FreePoint;

FreePoint* find_free_points(CFG* cfg, const char* var) {
    FreePoint* points = NULL;

    for (int i = 0; i < cfg->node_count; i++) {
        CFGNode* node = cfg->nodes[i];

        // Check if var is live_in but not live_out at any successor
        if (in_set(node->live_in, node->live_in_count, var)) {
            for (int j = 0; j < node->succ_count; j++) {
                if (!in_set(node->succs[j]->live_in,
                           node->succs[j]->live_in_count, var)) {
                    // var dies on this edge!
                    FreePoint* fp = malloc(sizeof(FreePoint));
                    fp->var_name = strdup(var);
                    fp->node = node;
                    fp->is_conditional = (node->succ_count > 1);
                    fp->next = points;
                    points = fp;
                }
            }
        }
    }

    return points;
}
```

### 6.4 Generate Path-Specific Frees

```c
void gen_nll_code(CFG* cfg, Value* expr) {
    // For each variable, find where to free
    char** all_vars = collect_all_vars(expr);

    for (int i = 0; all_vars[i]; i++) {
        FreePoint* fps = find_free_points(cfg, all_vars[i]);

        FreePoint* fp = fps;
        while (fp) {
            if (fp->is_conditional) {
                // Generate conditional free
                printf("// NLL: %s may be freed here on some paths\n", fp->var_name);
                printf("if (!_path_uses_%s) free_obj(%s);\n",
                       fp->var_name, fp->var_name);
            } else {
                // Unconditional free
                printf("// NLL: %s freed early (before scope end)\n", fp->var_name);
                printf("free_obj(%s);\n", fp->var_name);
            }
            fp = fp->next;
        }
    }
}

// Example output:
// Original:
//   let x = alloc()
//   if (cond) {
//     use(x)
//   } else {
//     // x not used
//   }
//   // scope end - old ASAP frees here
//
// With NLL:
//   let x = alloc()
//   if (cond) {
//     use(x)
//     free(x)  // NLL: freed immediately after last use
//   } else {
//     free(x)  // NLL: freed at branch start (never used)
//   }
//   // nothing to free here
```

---

## Phase 6b: Freeze Detection & SCC-based RC (ISMM 2024)

**Goal**: Handle cyclic immutable data with SCC-level reference counting.

**Reference**: [Reference Counting Deeply Immutable Data Structures with Cycles (ISMM 2024)](https://dl.acm.org/doi/10.1145/3652024.3665507)

**Status**: 🔲 Not Started

### Key Insight

For **frozen** (immutable) cyclic structures:
1. Calculate SCCs once at freeze time (almost linear via Tarjan's algorithm)
2. Single reference count per SCC, not per object
3. No backup cycle collector needed
4. Precise reachability, deterministic reclamation

### Tasks

- [ ] **6b.1** Detect freeze points (where data becomes immutable)
- [ ] **6b.2** Implement Tarjan's SCC algorithm
- [ ] **6b.3** Generate SCC wrapper structures
- [ ] **6b.4** Generate SCC-level reference counting
- [ ] **6b.5** Handle external references to SCC members
- [ ] **6b.6** Add tests for cyclic frozen data

### 6b.1 Freeze Point Detection

```c
// A freeze point is where cyclic data transitions to immutable
// Examples:
// - After construction of a cyclic graph
// - When data is passed to a function expecting immutable input
// - Explicit freeze annotation

typedef struct FreezePoint {
    int line_number;
    char* var_name;
    Value* expr;
    struct FreezePoint* next;
} FreezePoint;

FreezePoint* detect_freeze_points(Value* expr) {
    FreezePoint* points = NULL;

    // Look for patterns indicating immutability:
    // 1. No subsequent mutations to the variable
    // 2. Only read-only operations after construction
    // 3. Explicit (freeze expr) form

    detect_freeze_recursive(expr, &points, NULL);
    return points;
}
```

### 6b.2 Tarjan's SCC Algorithm

```c
// Standard Tarjan's algorithm for finding SCCs
// Run once at freeze time, O(V+E) almost linear

typedef struct SCCNode {
    int id;
    int lowlink;
    int on_stack;
    Obj* obj;
    struct SCCNode* next;
} SCCNode;

typedef struct SCC {
    int id;
    Obj** members;
    int member_count;
    int ref_count;           // Single RC for entire SCC
    struct SCC* next;
} SCC;

int scc_index = 0;
SCCNode* scc_stack = NULL;

void tarjan_dfs(Obj* v, SCC** sccs) {
    SCCNode* node = get_or_create_node(v);
    node->id = node->lowlink = scc_index++;
    node->on_stack = 1;
    push_stack(node);

    // For each edge v -> w
    for_each_child(v, w) {
        SCCNode* w_node = get_node(w);
        if (w_node == NULL) {
            tarjan_dfs(w, sccs);
            node->lowlink = min(node->lowlink, get_node(w)->lowlink);
        } else if (w_node->on_stack) {
            node->lowlink = min(node->lowlink, w_node->id);
        }
    }

    // If v is root of SCC
    if (node->lowlink == node->id) {
        SCC* scc = create_scc();
        SCCNode* w;
        do {
            w = pop_stack();
            w->on_stack = 0;
            add_to_scc(scc, w->obj);
        } while (w != node);
        scc->next = *sccs;
        *sccs = scc;
    }
}
```

### 6b.3 Generate SCC Wrapper

```c
// Generated code for SCC-level RC

printf("typedef struct SCCWrapper {\n");
printf("    int ref_count;\n");
printf("    int member_count;\n");
printf("    Obj** members;\n");
printf("} SCCWrapper;\n\n");

printf("SCCWrapper* freeze_cyclic(Obj* root) {\n");
printf("    // Compute SCCs using Tarjan's algorithm\n");
printf("    SCC* sccs = compute_sccs(root);\n");
printf("    // Wrap each SCC\n");
printf("    SCCWrapper* wrapper = malloc(sizeof(SCCWrapper));\n");
printf("    wrapper->ref_count = 1;\n");
printf("    wrapper->member_count = count_members(sccs);\n");
printf("    wrapper->members = collect_members(sccs);\n");
printf("    return wrapper;\n");
printf("}\n\n");

printf("void release_scc(SCCWrapper* scc) {\n");
printf("    scc->ref_count--;\n");
printf("    if (scc->ref_count == 0) {\n");
printf("        for (int i = 0; i < scc->member_count; i++) {\n");
printf("            free(scc->members[i]);\n");
printf("        }\n");
printf("        free(scc->members);\n");
printf("        free(scc);\n");
printf("    }\n");
printf("}\n");
```

### 6b.4 Integration with ASAP

```c
// In h_let_codegen, after shape analysis:

if (shape == SHAPE_CYCLIC) {
    // Check if this becomes immutable
    if (is_frozen_after_construction(var, body)) {
        // Use ISMM 2024 approach
        emit("SCCWrapper* %s_scc = freeze_cyclic(%s);\n", var, var);
        // ... use %s_scc instead of %s ...
        emit("release_scc(%s_scc);\n", var);
    } else {
        // Fallback to deferred RC
        emit("deferred_release(%s);\n", var);
    }
}
```

---

## Phase 7: Deferred RC Fallback

**Goal**: Handle mutable cyclic data that never freezes (edge case).

**Status**: 🔲 Not Started

### Key Insight

For the rare case of mutable cycles:
1. Zero-count objects added to deferred list
2. Bounded processing at safe points (O(k) per point)
3. No heap traversal, just process the list
4. Eventually freed, zero-pause guaranteed

### Tasks

- [ ] **7.1** Implement deferred list data structure
- [ ] **7.2** Add safe points at function boundaries
- [ ] **7.3** Implement bounded processing
- [ ] **7.4** Root scanning for cycle confirmation
- [ ] **7.5** Add tests

### 7.1 Deferred List

```c
// Already partially implemented as FREE_LIST, enhance it:

typedef struct DeferredNode {
    Obj* obj;
    int deferred_at;        // Timestamp/counter when added
    int retry_count;        // How many times we've tried to free
    struct DeferredNode* next;
} DeferredNode;

DeferredNode* DEFERRED_HEAD = NULL;
int DEFERRED_COUNT = 0;
int SAFE_POINT_COUNTER = 0;

void deferred_release(Obj* x) {
    if (!x) return;
    x->mark--;  // Decrement refcount

    if (x->mark == 0) {
        // Might be dead, add to deferred list
        DeferredNode* n = malloc(sizeof(DeferredNode));
        n->obj = x;
        n->deferred_at = SAFE_POINT_COUNTER;
        n->retry_count = 0;
        n->next = DEFERRED_HEAD;
        DEFERRED_HEAD = n;
        DEFERRED_COUNT++;
    }
}
```

### 7.2 Safe Points

```c
// Insert at function entry/exit and loop iterations

void safe_point() {
    SAFE_POINT_COUNTER++;

    // Process bounded amount of deferred list
    int budget = 10;  // Constant k

    while (DEFERRED_HEAD && budget > 0) {
        DeferredNode* n = DEFERRED_HEAD;
        DEFERRED_HEAD = n->next;
        DEFERRED_COUNT--;

        // Confirm still dead (no external refs)
        if (n->obj->mark == 0) {
            // Free children first
            if (n->obj->a) deferred_release(n->obj->a);
            if (n->obj->b) deferred_release(n->obj->b);
            free(n->obj);
        }
        // If mark > 0, someone resurrected it, drop from list

        free(n);
        budget--;
    }
}
```

### 7.3 Generate Safe Points

```c
// In code generation, insert safe points:

void gen_function_entry(const char* name) {
    printf("void %s(...) {\n", name);
    printf("    safe_point();  // Deferred RC processing\n");
}

void gen_function_exit() {
    printf("    safe_point();  // Deferred RC processing\n");
    printf("}\n");
}

void gen_loop_iteration() {
    printf("    safe_point();  // Deferred RC processing\n");
}
```

---

## Phase 8: Arena Allocation (Optimization)

**Goal**: Bulk allocate/deallocate for cyclic structures that don't escape.

**Reference**: [Arena Allocators](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)

### Tasks

- [ ] **7.1** Detect arena-eligible allocations
- [ ] **7.2** Generate arena runtime
- [ ] **7.3** Transform allocation sites
- [ ] **7.4** Insert arena destruction
- [ ] **7.5** Add tests

### 7.1 Detect Arena-Eligible Allocations

```c
typedef struct ArenaScope {
    int id;
    int start_line;
    int end_line;
    char** allocated_vars;
    int var_count;
    int is_cyclic;
    struct ArenaScope* next;
} ArenaScope;

// Check if allocations in a scope form cycles but don't escape
ArenaScope* find_arena_scopes(Value* expr, ShapeContext* shapes) {
    ArenaScope* scopes = NULL;

    find_arena_scopes_recursive(expr, shapes, &scopes, NULL);

    return scopes;
}

void find_arena_scopes_recursive(Value* expr, ShapeContext* shapes,
                                  ArenaScope** scopes, ArenaScope* current) {
    if (!expr || is_nil(expr)) return;

    if (expr->tag == T_CELL) {
        Value* op = car(expr);

        if (op->tag == T_SYM && strcmp(op->s, "let") == 0) {
            // Start a new potential arena scope
            ArenaScope* scope = malloc(sizeof(ArenaScope));
            scope->id = (*scopes) ? (*scopes)->id + 1 : 1;
            scope->allocated_vars = malloc(100 * sizeof(char*));
            scope->var_count = 0;
            scope->is_cyclic = 0;

            Value* bindings = car(cdr(expr));
            Value* body = car(cdr(cdr(expr)));

            // Analyze bindings
            while (!is_nil(bindings)) {
                Value* bind = car(bindings);
                Value* sym = car(bind);
                Value* val = car(cdr(bind));

                Shape shape = analyze_shape_expr(val, shapes);
                if (shape == SHAPE_CYCLIC) {
                    scope->is_cyclic = 1;
                }

                // Check if escapes
                if (!escapes_scope(sym->s, body)) {
                    scope->allocated_vars[scope->var_count++] = strdup(sym->s);
                }

                bindings = cdr(bindings);
            }

            // Only create arena if cyclic and has non-escaping vars
            if (scope->is_cyclic && scope->var_count > 0) {
                scope->next = *scopes;
                *scopes = scope;

                // Recurse with this scope
                find_arena_scopes_recursive(body, shapes, scopes, scope);
            } else {
                free(scope);
                find_arena_scopes_recursive(body, shapes, scopes, current);
            }
            return;
        }
    }

    // Recurse on children
    if (expr->tag == T_CELL) {
        find_arena_scopes_recursive(car(expr), shapes, scopes, current);
        find_arena_scopes_recursive(cdr(expr), shapes, scopes, current);
    }
}
```

### 7.2 Generate Arena Runtime

```c
void gen_arena_runtime() {
    printf("// Arena allocator for cyclic structures\n\n");

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
    printf("} Arena;\n\n");

    printf("Arena* arena_create(size_t block_size) {\n");
    printf("    Arena* a = malloc(sizeof(Arena));\n");
    printf("    a->block_size = block_size ? block_size : 4096;\n");
    printf("    a->blocks = NULL;\n");
    printf("    a->current = NULL;\n");
    printf("    return a;\n");
    printf("}\n\n");

    printf("void* arena_alloc(Arena* a, size_t size) {\n");
    printf("    // Align to 8 bytes\n");
    printf("    size = (size + 7) & ~7;\n");
    printf("    \n");
    printf("    if (!a->current || a->current->used + size > a->current->size) {\n");
    printf("        // Need new block\n");
    printf("        size_t block_size = a->block_size;\n");
    printf("        if (size > block_size) block_size = size;\n");
    printf("        \n");
    printf("        ArenaBlock* b = malloc(sizeof(ArenaBlock));\n");
    printf("        b->memory = malloc(block_size);\n");
    printf("        b->size = block_size;\n");
    printf("        b->used = 0;\n");
    printf("        b->next = a->blocks;\n");
    printf("        a->blocks = b;\n");
    printf("        a->current = b;\n");
    printf("    }\n");
    printf("    \n");
    printf("    void* ptr = a->current->memory + a->current->used;\n");
    printf("    a->current->used += size;\n");
    printf("    return ptr;\n");
    printf("}\n\n");

    printf("void arena_destroy(Arena* a) {\n");
    printf("    ArenaBlock* b = a->blocks;\n");
    printf("    while (b) {\n");
    printf("        ArenaBlock* next = b->next;\n");
    printf("        free(b->memory);\n");
    printf("        free(b);\n");
    printf("        b = next;\n");
    printf("    }\n");
    printf("    free(a);\n");
    printf("}\n\n");

    printf("// Arena-aware allocators\n");
    printf("Obj* arena_mk_int(Arena* a, long val) {\n");
    printf("    Obj* o = arena_alloc(a, sizeof(Obj));\n");
    printf("    o->tag = T_INT;\n");
    printf("    o->i = val;\n");
    printf("    return o;\n");
    printf("}\n\n");

    printf("Obj* arena_mk_pair(Arena* a, Obj* car, Obj* cdr) {\n");
    printf("    Obj* o = arena_alloc(a, sizeof(Obj));\n");
    printf("    o->tag = T_PAIR;\n");
    printf("    o->a = car;\n");
    printf("    o->b = cdr;\n");
    printf("    return o;\n");
    printf("}\n\n");
}
```

### 7.3 Transform Allocation Sites

```c
void gen_arena_scope(ArenaScope* scope, Value* body) {
    printf("{\n");
    printf("    // ARENA SCOPE %d - cyclic allocations\n", scope->id);
    printf("    Arena* _arena_%d = arena_create(0);\n", scope->id);

    // Transform allocations to use arena
    gen_body_with_arena(body, scope);

    printf("    arena_destroy(_arena_%d);\n", scope->id);
    printf("}\n");
}

void gen_body_with_arena(Value* body, ArenaScope* scope) {
    // Replace mk_* calls with arena_mk_* for vars in scope
    // ... recursive transformation ...
}
```

---

## Phase 8: Exception Handling

**Goal**: Generate cleanup code for stack unwinding.

**Reference**: [LLVM Exception Handling](https://llvm.org/docs/ExceptionHandling.html)

### Tasks

- [ ] **8.1** Track live allocations at each point
- [ ] **8.2** Generate landing pads
- [ ] **8.3** Generate cleanup tables
- [ ] **8.4** Handle nested try/catch
- [ ] **8.5** Add tests

### 8.1 Track Live Allocations

```c
typedef struct LiveAlloc {
    char* var_name;
    int alloc_line;
    int scope_depth;
    struct LiveAlloc* next;
} LiveAlloc;

typedef struct CleanupPoint {
    int line;
    LiveAlloc* live_allocs;   // What to clean at this point
    struct CleanupPoint* next;
} CleanupPoint;

// Build cleanup info for a function
CleanupPoint* build_cleanup_info(Value* func_body) {
    CleanupPoint* points = NULL;
    LiveAlloc* current_live = NULL;
    int scope_depth = 0;

    build_cleanup_recursive(func_body, &points, &current_live, &scope_depth, 1);

    return points;
}

void build_cleanup_recursive(Value* expr, CleanupPoint** points,
                             LiveAlloc** live, int* depth, int line) {
    if (!expr || is_nil(expr)) return;

    if (expr->tag == T_CELL) {
        Value* op = car(expr);

        // Function call - potential throw point
        if (is_call(expr)) {
            // Record cleanup point
            CleanupPoint* cp = malloc(sizeof(CleanupPoint));
            cp->line = line;
            cp->live_allocs = copy_live_list(*live);
            cp->next = *points;
            *points = cp;
        }

        // Let - adds to live set
        if (op->tag == T_SYM && strcmp(op->s, "let") == 0) {
            (*depth)++;

            Value* bindings = car(cdr(expr));
            while (!is_nil(bindings)) {
                Value* bind = car(bindings);
                Value* sym = car(bind);

                // Add to live set
                LiveAlloc* la = malloc(sizeof(LiveAlloc));
                la->var_name = strdup(sym->s);
                la->alloc_line = line;
                la->scope_depth = *depth;
                la->next = *live;
                *live = la;

                bindings = cdr(bindings);
                line++;
            }

            // Process body
            build_cleanup_recursive(car(cdr(cdr(expr))), points, live, depth, line);

            // Remove from live set (scope exit)
            remove_at_depth(live, *depth);
            (*depth)--;
            return;
        }
    }

    // Recurse
    if (expr->tag == T_CELL) {
        build_cleanup_recursive(car(expr), points, live, depth, line);
        build_cleanup_recursive(cdr(expr), points, live, depth, line + 1);
    }
}
```

### 8.2 Generate Landing Pads

```c
void gen_landing_pads(CleanupPoint* points) {
    printf("// Exception landing pads\n\n");

    CleanupPoint* cp = points;
    int pad_id = 0;

    while (cp) {
        printf("_landing_pad_%d:\n", pad_id);
        printf("    // Cleanup for throw at line %d\n", cp->line);

        // Free in reverse order of allocation
        LiveAlloc* la = cp->live_allocs;
        while (la) {
            printf("    free_obj(%s);\n", la->var_name);
            la = la->next;
        }

        printf("    _Unwind_Resume(_exception);\n\n");

        cp = cp->next;
        pad_id++;
    }
}
```

### 8.3 Generate Cleanup Tables

```c
void gen_cleanup_table(CleanupPoint* points, const char* func_name) {
    printf("// LSDA (Language-Specific Data Area) for %s\n", func_name);
    printf("static const struct {\n");
    printf("    int start_line;\n");
    printf("    int end_line;\n");
    printf("    int landing_pad;\n");
    printf("    int num_cleanups;\n");
    printf("} _cleanup_table_%s[] = {\n", func_name);

    CleanupPoint* cp = points;
    int pad_id = 0;
    int prev_line = 0;

    while (cp) {
        int num_cleanups = count_live(cp->live_allocs);
        printf("    {%d, %d, %d, %d},\n",
               prev_line, cp->line, pad_id, num_cleanups);
        prev_line = cp->line;
        cp = cp->next;
        pad_id++;
    }

    printf("    {0, 0, 0, 0}  // sentinel\n");
    printf("};\n\n");
}
```

---

## Phase 9: Concurrency Support

**Goal**: Handle ownership transfer across threads.

**Reference**: [SOTER - Ownership Transfer](https://experts.illinois.edu/en/publications/inferring-ownership-transfer-for-efficient-message-passing)

### Tasks

- [ ] **9.1** Detect thread spawn points
- [ ] **9.2** Identify message send/receive
- [ ] **9.3** Infer ownership transfer
- [ ] **9.4** Generate thread-safe code
- [ ] **9.5** Add tests

### 9.1-9.4 Implementation

```c
typedef enum {
    OWNER_LOCAL,          // Thread-local, pure ASAP
    OWNER_TRANSFERRED,    // Ownership moved to another thread
    OWNER_SHARED          // Shared between threads (needs atomic RC)
} Ownership;

typedef struct ThreadAnalysis {
    char* var_name;
    Ownership ownership;
    int transfer_line;      // Where ownership transferred
    char* transfer_target;  // Channel or thread
} ThreadAnalysis;

// Detect message passing patterns
void analyze_concurrency(Value* expr, ThreadAnalysis** analysis) {
    if (!expr || is_nil(expr)) return;

    if (expr->tag == T_CELL) {
        Value* op = car(expr);

        if (op->tag == T_SYM) {
            // Channel send - ownership transfer
            if (strcmp(op->s, "channel-send") == 0 ||
                strcmp(op->s, "send!") == 0) {
                Value* channel = car(cdr(expr));
                Value* value = car(cdr(cdr(expr)));

                if (value->tag == T_SYM) {
                    ThreadAnalysis* ta = malloc(sizeof(ThreadAnalysis));
                    ta->var_name = strdup(value->s);
                    ta->ownership = OWNER_TRANSFERRED;
                    ta->transfer_target = strdup(channel->s);
                    ta->next = *analysis;
                    *analysis = ta;
                }
            }

            // Thread spawn with closure
            if (strcmp(op->s, "spawn") == 0 ||
                strcmp(op->s, "thread-create") == 0) {
                Value* closure = car(cdr(expr));

                // Find captured variables - they transfer ownership
                char** captured = find_captured_vars(closure);
                for (int i = 0; captured[i]; i++) {
                    ThreadAnalysis* ta = malloc(sizeof(ThreadAnalysis));
                    ta->var_name = strdup(captured[i]);
                    ta->ownership = OWNER_TRANSFERRED;
                    ta->transfer_target = "spawned_thread";
                    ta->next = *analysis;
                    *analysis = ta;
                }
            }
        }
    }

    // Recurse
    if (expr->tag == T_CELL) {
        analyze_concurrency(car(expr), analysis);
        analyze_concurrency(cdr(expr), analysis);
    }
}

// Generate code based on ownership analysis
void gen_concurrent_code(Value* expr, ThreadAnalysis* analysis) {
    // For transferred vars: don't free in sender
    // For shared vars: use atomic refcount
    // For local vars: pure ASAP

    printf("// Concurrency-aware memory management\n");

    ThreadAnalysis* ta = analysis;
    while (ta) {
        switch (ta->ownership) {
            case OWNER_TRANSFERRED:
                printf("// %s: ownership transferred to %s - no free here\n",
                       ta->var_name, ta->transfer_target);
                break;
            case OWNER_SHARED:
                printf("// %s: shared - using atomic refcount\n", ta->var_name);
                break;
            case OWNER_LOCAL:
                printf("// %s: thread-local - pure ASAP\n", ta->var_name);
                break;
        }
        ta = ta->next;
    }
}
```

---

## Phase 10: Region Polymorphism

**Goal**: Handle generic functions with region-parameterized lifetimes.

**Reference**: [Tofte-Talpin Region Inference](https://www.semanticscholar.org/paper/Region-based-Memory-Management-Tofte-Talpin/9117c75f62162b0bcf8e1ab91b7e25e0acc919a8)

### Tasks

- [ ] **10.1** Infer region parameters for functions
- [ ] **10.2** Specialize when regions known at call site
- [ ] **10.3** Generate runtime region passing when needed
- [ ] **10.4** Handle higher-order functions
- [ ] **10.5** Add tests

### Implementation

```c
typedef struct RegionParam {
    char* name;           // ρ1, ρ2, etc.
    int is_input;         // From parameter
    int is_output;        // In return value
} RegionParam;

typedef struct RegionSignature {
    char* func_name;
    RegionParam** params;
    int param_count;
    char* return_region;
} RegionSignature;

// Infer region signature for a function
RegionSignature* infer_regions(Value* func_def) {
    char* name = get_func_name(func_def);
    Value* params = get_func_params(func_def);
    Value* body = get_func_body(func_def);

    RegionSignature* sig = malloc(sizeof(RegionSignature));
    sig->func_name = strdup(name);
    sig->params = malloc(10 * sizeof(RegionParam*));
    sig->param_count = 0;

    // Each parameter gets a region variable
    int region_id = 1;
    Value* p = params;
    while (!is_nil(p)) {
        RegionParam* rp = malloc(sizeof(RegionParam));
        char buf[32];
        sprintf(buf, "ρ%d", region_id++);
        rp->name = strdup(buf);
        rp->is_input = 1;
        rp->is_output = 0;
        sig->params[sig->param_count++] = rp;
        p = cdr(p);
    }

    // Analyze body for return region
    sig->return_region = infer_return_region(body, sig);

    return sig;
}

// Generate monomorphized version when regions known
void gen_monomorphic(RegionSignature* sig, char** concrete_regions) {
    printf("// Monomorphized %s with regions [", sig->func_name);
    for (int i = 0; i < sig->param_count; i++) {
        printf("%s", concrete_regions[i]);
        if (i < sig->param_count - 1) printf(", ");
    }
    printf("]\n");

    printf("Obj* %s_mono(", sig->func_name);
    // Generate with concrete regions...
}

// Generate polymorphic version with runtime region passing
void gen_polymorphic(RegionSignature* sig) {
    printf("// Polymorphic %s - regions passed at runtime\n", sig->func_name);
    printf("Obj* %s_poly(", sig->func_name);

    // Region parameters first
    for (int i = 0; i < sig->param_count; i++) {
        printf("Region* %s, ", sig->params[i]->name);
    }

    // Then value parameters
    // ...

    printf(") {\n");
    printf("    // Allocate in passed regions\n");
    printf("}\n");
}
```

---

## Phase 11: Incremental Analysis

**Goal**: Cache and reuse analysis results across compilations.

**Reference**: [SILVA - Incremental Analysis](https://dl.acm.org/doi/10.1145/3725214)

### Tasks

- [ ] **11.1** Define function summary format
- [ ] **11.2** Implement summary cache
- [ ] **11.3** Detect changed functions
- [ ] **11.4** Propagate changes through call graph
- [ ] **11.5** Add tests

### Implementation

```c
typedef struct FunctionSummary {
    char* name;
    uint64_t hash;            // Hash of function body

    // Shape summary
    Shape* param_shapes;
    Shape return_shape;

    // Region summary
    RegionSignature* regions;

    // Escape summary
    int* param_escapes;       // Which params escape

    // Allocation summary
    int allocates;            // Does it allocate?
    int allocation_count;     // How many allocations

    // Dependencies
    char** called_functions;
    int call_count;

    // Timestamp
    time_t last_updated;
} FunctionSummary;

// Summary cache file format
void save_summary_cache(FunctionSummary** summaries, int count,
                        const char* cache_file) {
    FILE* f = fopen(cache_file, "wb");

    // Header
    fprintf(f, "ASAP_CACHE_V1\n");
    fprintf(f, "count:%d\n", count);

    for (int i = 0; i < count; i++) {
        FunctionSummary* s = summaries[i];
        fprintf(f, "---\n");
        fprintf(f, "name:%s\n", s->name);
        fprintf(f, "hash:%lx\n", s->hash);
        fprintf(f, "return_shape:%d\n", s->return_shape);
        // ... more fields ...
    }

    fclose(f);
}

FunctionSummary** load_summary_cache(const char* cache_file, int* count) {
    FILE* f = fopen(cache_file, "rb");
    if (!f) return NULL;

    // Parse cache file...

    return summaries;
}

// Incremental update
void incremental_analyze(Value* program, const char* cache_file) {
    int cached_count;
    FunctionSummary** cached = load_summary_cache(cache_file, &cached_count);

    // Find changed functions
    Value* defs = program;
    while (!is_nil(defs)) {
        Value* def = car(defs);
        if (is_function_def(def)) {
            char* name = get_func_name(def);
            uint64_t hash = hash_function_body(get_func_body(def));

            FunctionSummary* cached_sum = find_summary(cached, cached_count, name);

            if (!cached_sum || cached_sum->hash != hash) {
                // Function changed - reanalyze
                printf("// Reanalyzing %s (changed)\n", name);
                FunctionSummary* new_sum = analyze_function(def);
                update_cache(cached, &cached_count, new_sum);

                // Mark dependents as needing update
                mark_dependents_dirty(cached, cached_count, name);
            }
        }
        defs = cdr(defs);
    }

    // Propagate changes
    propagate_changes(cached, cached_count);

    // Save updated cache
    save_summary_cache(cached, cached_count, cache_file);
}
```

---

## Phase 12: CactusRef-Style Local Cycle Detection

**Goal**: Deterministic O(cycle_size) cycle detection on drop.

**Reference**: [CactusRef](https://github.com/artichoke/cactusref)

### Tasks

- [ ] **12.1** Add internal link tracking to objects
- [ ] **12.2** Implement adopt/unadopt operations
- [ ] **12.3** Implement orphan cycle detection
- [ ] **12.4** Generate code for cycle-aware types
- [ ] **12.5** Add tests

### Implementation

```c
// Extended object header for cycle detection
typedef struct CycleAwareObj {
    int rc;                    // External reference count
    int internal_rc;           // References from within potential cycle
    int color;                 // WHITE, GRAY, BLACK for traversal
    struct CycleLink* links;   // Tracked ownership links
} CycleAwareObj;

typedef struct CycleLink {
    CycleAwareObj* target;
    struct CycleLink* next;
} CycleLink;

// Adopt: track a new internal link
void gen_adopt() {
    printf("void adopt(CycleAwareObj* parent, CycleAwareObj* child) {\n");
    printf("    if (!parent || !child) return;\n");
    printf("    \n");
    printf("    // Record the link\n");
    printf("    CycleLink* link = malloc(sizeof(CycleLink));\n");
    printf("    link->target = child;\n");
    printf("    link->next = parent->links;\n");
    printf("    parent->links = link;\n");
    printf("    \n");
    printf("    // Increment internal ref count\n");
    printf("    child->internal_rc++;\n");
    printf("}\n\n");
}

// Unadopt: remove internal link tracking
void gen_unadopt() {
    printf("void unadopt(CycleAwareObj* parent, CycleAwareObj* child) {\n");
    printf("    if (!parent || !child) return;\n");
    printf("    \n");
    printf("    // Remove from link list\n");
    printf("    CycleLink** pp = &parent->links;\n");
    printf("    while (*pp) {\n");
    printf("        if ((*pp)->target == child) {\n");
    printf("            CycleLink* to_free = *pp;\n");
    printf("            *pp = (*pp)->next;\n");
    printf("            free(to_free);\n");
    printf("            child->internal_rc--;\n");
    printf("            return;\n");
    printf("        }\n");
    printf("        pp = &(*pp)->next;\n");
    printf("    }\n");
    printf("}\n\n");
}

// Check if cycle is orphaned (no external refs)
void gen_is_orphaned_cycle() {
    printf("// BFS to check if cycle is orphaned\n");
    printf("int is_orphaned_cycle(CycleAwareObj* start) {\n");
    printf("    // Queue for BFS\n");
    printf("    CycleAwareObj* queue[1024];\n");
    printf("    int queue_start = 0, queue_end = 0;\n");
    printf("    \n");
    printf("    // Mark start\n");
    printf("    start->color = GRAY;\n");
    printf("    queue[queue_end++] = start;\n");
    printf("    \n");
    printf("    while (queue_start < queue_end) {\n");
    printf("        CycleAwareObj* obj = queue[queue_start++];\n");
    printf("        \n");
    printf("        // Check if externally reachable\n");
    printf("        if (obj->rc > obj->internal_rc) {\n");
    printf("            // Has external refs - not orphaned\n");
    printf("            clear_colors(start);  // Reset colors\n");
    printf("            return 0;\n");
    printf("        }\n");
    printf("        \n");
    printf("        // Add linked objects to queue\n");
    printf("        CycleLink* link = obj->links;\n");
    printf("        while (link) {\n");
    printf("            if (link->target->color == WHITE) {\n");
    printf("                link->target->color = GRAY;\n");
    printf("                queue[queue_end++] = link->target;\n");
    printf("            }\n");
    printf("            link = link->next;\n");
    printf("        }\n");
    printf("        \n");
    printf("        obj->color = BLACK;\n");
    printf("    }\n");
    printf("    \n");
    printf("    // All objects in cycle have rc == internal_rc\n");
    printf("    // Cycle is orphaned!\n");
    printf("    return 1;\n");
    printf("}\n\n");
}

// Free an orphaned cycle
void gen_free_cycle() {
    printf("void free_cycle(CycleAwareObj* start) {\n");
    printf("    // Collect all BLACK objects (the cycle)\n");
    printf("    CycleAwareObj* to_free[1024];\n");
    printf("    int count = 0;\n");
    printf("    \n");
    printf("    collect_black(start, to_free, &count);\n");
    printf("    \n");
    printf("    // Free all collected objects\n");
    printf("    for (int i = 0; i < count; i++) {\n");
    printf("        // Clear links first\n");
    printf("        CycleLink* link = to_free[i]->links;\n");
    printf("        while (link) {\n");
    printf("            CycleLink* next = link->next;\n");
    printf("            free(link);\n");
    printf("            link = next;\n");
    printf("        }\n");
    printf("        free(to_free[i]);\n");
    printf("    }\n");
    printf("}\n\n");
}

// Drop with cycle detection
void gen_cycle_aware_drop() {
    printf("void drop_cycle_aware(CycleAwareObj* obj) {\n");
    printf("    if (!obj) return;\n");
    printf("    \n");
    printf("    obj->rc--;\n");
    printf("    \n");
    printf("    if (obj->rc == 0) {\n");
    printf("        // Definitely garbage - free immediately\n");
    printf("        CycleLink* link = obj->links;\n");
    printf("        while (link) {\n");
    printf("            link->target->internal_rc--;\n");
    printf("            drop_cycle_aware(link->target);\n");
    printf("            CycleLink* next = link->next;\n");
    printf("            free(link);\n");
    printf("            link = next;\n");
    printf("        }\n");
    printf("        free(obj);\n");
    printf("    }\n");
    printf("    else if (obj->rc == obj->internal_rc && obj->internal_rc > 0) {\n");
    printf("        // All refs are internal - might be orphaned cycle\n");
    printf("        if (is_orphaned_cycle(obj)) {\n");
    printf("            free_cycle(obj);\n");
    printf("        }\n");
    printf("    }\n");
    printf("    // else: still has external refs, do nothing\n");
    printf("}\n\n");
}
```

---

## Testing Strategy

### Unit Tests Per Phase

```bash
#!/bin/bash
# test_all_phases.sh

echo "=== Phase 2: Shape Analysis ==="
run_test "Shape-Tree" "(cons 1 2)" "SHAPE_TREE"
run_test "Shape-DAG" "(let ((x 1)) (cons x x))" "SHAPE_DAG"

echo "=== Phase 3: Auto-Weak ==="
run_test "AutoWeak" "(defstruct DLL (val next prev))" "FIELD_WEAK"

echo "=== Phase 4: Perceus Reuse ==="
run_test "Reuse" "(let ((x (mk 1))) (let ((y (mk 2))) y))" "try_reuse"

echo "=== Phase 5: DPS ==="
run_test "DPS" "(map f xs)" "_dps"

echo "=== Phase 6: NLL ==="
run_test "NLL" "(if c (use x) skip)" "free early"

echo "=== Phase 7: Arena ==="
run_test "Arena" "(build-graph)" "arena_destroy"

echo "=== Phase 8: Exceptions ==="
run_test "Cleanup" "(may-throw)" "landing_pad"

echo "=== Phase 9: Concurrency ==="
run_test "Transfer" "(send ch x)" "OWNER_TRANSFERRED"

echo "=== Phase 10: Regions ==="
run_test "Region" "(map f xs)" "Region*"

echo "=== Phase 11: Incremental ==="
run_test "Cache" "..." "CACHE_HIT"

echo "=== Phase 12: CactusRef ==="
run_test "Cycle" "(make-cycle)" "is_orphaned_cycle"
```

---

## Summary Checklist

### Core (Done)
- [x] VarUsage infrastructure
- [x] Liveness analysis
- [x] Escape analysis
- [x] Capture tracking
- [x] Multi-binding let

### Shape & Types (Done)
- [x] Shape analysis (Tree/DAG/Cyclic) - Phase 2
- [x] Typed reference fields - Phase 3
- [x] Auto-weak back-edge detection - Phase 3

### Optimizations (Partial)
- [x] Perceus reuse analysis - Phase 4
- [ ] Destination-passing style - Phase 5
- [x] Non-lexical lifetimes - Phase 6

### Memory Strategies
- [ ] Arena allocation - Phase 7
- [ ] CactusRef local cycle detection - Phase 12

### Advanced
- [ ] Exception handling (landing pads) - Phase 8
- [ ] Concurrency (ownership transfer) - Phase 9
- [ ] Region polymorphism - Phase 10
- [ ] Incremental analysis - Phase 11

---

## References

1. [ASAP: As Static As Possible](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf)
2. [Perceus: Garbage Free RC](https://dl.acm.org/doi/10.1145/3453483.3454032)
3. [Destination-Passing Style](https://dl.acm.org/doi/10.1145/3122948.3122949)
4. [CactusRef](https://github.com/artichoke/cactusref)
5. [Shape Analysis - Ghiya & Hendren](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-A-shape-for-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040)
6. [Typed Reference Counting](https://www.sciencedirect.com/science/article/abs/pii/S1477842411000285)
7. [LLVM Exception Handling](https://llvm.org/docs/ExceptionHandling.html)
8. [Region-Based Memory - Tofte & Talpin](https://www.semanticscholar.org/paper/Region-based-Memory-Management-Tofte-Talpin/9117c75f62162b0bcf8e1ab91b7e25e0acc919a8)
9. [SILVA: Incremental Analysis](https://dl.acm.org/doi/10.1145/3725214)
10. [Arena Allocators](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator)
