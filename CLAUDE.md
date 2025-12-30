# Purple C Scratch - ASAP Memory Management

## CRITICAL: ASAP is NOT Garbage Collection

**ASAP (As Static As Possible)** is a **compile-time static memory management** strategy.
It does NOT use runtime garbage collection.

### Core Principle

The compiler analyzes the program and **statically inserts `free()` calls** at the optimal
points during code generation. All deallocation decisions are made at compile time.

```
WRONG: Runtime GC that scans heap and collects garbage
RIGHT: Compiler injects free_obj(x) at compile time based on static analysis
```

### What ASAP Does

1. **CLEAN Phase** (compile-time)
   - Analyzes variable lifetimes statically
   - Injects `free_obj()` calls at scope exit (or earlier based on liveness)
   - Variables captured by closures are NOT freed (ownership transfers to closure)

2. **Liveness Analysis** (compile-time)
   - Tracks last use of each variable
   - Can free earlier than scope exit if variable is dead

3. **Escape Analysis** (compile-time)
   - `ESCAPE_NONE`: Value stays local → can stack-allocate
   - `ESCAPE_ARG`: Escapes via function argument → heap-allocate
   - `ESCAPE_GLOBAL`: Escapes to return/closure → heap-allocate, careful with freeing

4. **Capture Tracking** (compile-time)
   - Identifies variables captured by lambdas/closures
   - These variables must NOT be freed in parent scope

### What Scanners Are For

The `scan_List()` function is a **traversal utility**, NOT a garbage collector:
- Debugging (checking what's reachable)
- Manual reference counting updates
- Runtime verification in debug builds
- Marking for other static analyses

### Deferred Free List

The `FREE_LIST` is an optimization for **batching frees**, not a GC mechanism:
- Prevents issues during complex traversals
- Allows flushing at safe points
- NOT for mark-sweep collection

## Implementation Status

### Completed Optimizations

| Phase | Feature | Description |
|-------|---------|-------------|
| 1 | VarUsage Infrastructure | Track variable usage, escape, capture status |
| 2 | Liveness Analysis | Free at last-use, not just scope-end |
| 3 | Escape Analysis | Stack-allocate non-escaping values |
| 4 | Capture Tracking | Don't free lambda-captured variables |
| 5 | Dynamic Free List | Linked list instead of fixed array |
| 6 | Type-Aware Scanners | Traversal utilities (NOT GC) |
| 7 | Multi-Binding Let | Support `(let ((x 1) (y 2)) body)` |
| 8 | Field-Aware Scanners | Skip non-pointer fields in traversal |

## Key Files

- `main.c` - Single-file implementation (~1100 lines)
- `tests.sh` - Regression tests (14 tests)
- `examples/demo.purple` - Example programs

## References

- *ASAP: As Static As Possible memory management* (Proust, 2017)
- *Collapsing Towers of Interpreters* (Amin & Rompf, POPL 2018)
- *Better Static Memory Management* (Aiken et al., PLDI 1995)
- *Region-Based Memory Management* (Tofte & Talpin, 1997)

---

## Research-Based Optimization Opportunities

### 1. Perceus: Reuse Analysis (PLDI 2021 - Distinguished Paper)

**Source**: [Perceus: Garbage Free Reference Counting with Reuse](https://dl.acm.org/doi/10.1145/3453483.3454032)

Perceus (from Koka language) introduces **reuse analysis** - pairing deallocations with
allocations of the same size to enable in-place updates.

**How to apply to ASAP**:
```
Current:  free_obj(x); ... y = mk_int(42);
Improved: y = reuse_as_int(x, 42);  // Reuse x's memory for y
```

**Implementation idea**:
- Track `free_obj()` calls and subsequent `mk_*()` calls
- If sizes match, emit `reuse_as_*()` instead of free+alloc
- Enables "Functional But In-Place" (FBIP) programming

### 2. Destination-Passing Style (FHPC 2017)

**Source**: [Destination-passing style for efficient memory management](https://dl.acm.org/doi/10.1145/3122948.3122949)

Instead of returning newly allocated values, pass a "destination" pointer
where the result should be written.

**How to apply to ASAP**:
```scheme
;; Current: allocates new list
(map f xs)

;; DPS: writes into pre-allocated destination
(map-into! dest f xs)
```

**Benefits**:
- Eliminates intermediate allocations
- Enables stack allocation of results
- Constant memory for pipelines

### 3. Region Inference (MLKit)

**Source**: [MLKit Compiler](https://elsman.com/mlkit/)

MLKit infers region annotations automatically - grouping objects with
similar lifetimes into regions that are freed together.

**How to apply to ASAP**:
- Instead of per-object free, group related objects into regions
- Free entire region when all objects are dead
- Reduces number of free calls

```c
// Current
free_obj(a); free_obj(b); free_obj(c);

// With regions
free_region(r1); // Frees a, b, c together
```

### 4. MemFix: Exact Cover Problem (ESEC/FSE 2018)

**Source**: [MemFix: Static Analysis-Based Repair](https://dl.acm.org/doi/10.1145/3236024.3236079)

Formulates free placement as an **exact cover problem** - finding a set
of deallocation points that:
- Frees every allocated object exactly once
- Avoids double-free and use-after-free

**How to apply to ASAP**:
- Current: heuristic-based free placement
- Improved: solve constraint system for optimal placement
- Guarantees correctness by construction

### 5. Non-Lexical Lifetimes (Rust/Polonius)

**Source**: [Rust Borrow Checker Dev Guide](https://rustc-dev-guide.rust-lang.org/borrow_check.html)

Rust's Polonius uses control-flow graph analysis instead of lexical scopes.

**How to apply to ASAP**:
```scheme
(let ((x (lift 10)))
  (if condition
    (use x)      ;; x live here
    (other))     ;; x dead here - could free earlier!
  (more-code))   ;; Currently waits until here to free
```

- Analyze CFG to find earliest safe free point per path
- Different branches can free at different points

### 6. Linear Types for Guaranteed Static Management

**Source**: [Practical Affine Types](https://users.cs.northwestern.edu/~jesse/pubs/alms/tovpucella-alms.pdf)

If the source language has linear/affine types, ASAP becomes trivial:
- Linear: used exactly once → free immediately after use
- Affine: used at most once → free at scope end if unused

**How to apply**:
- Add optional type annotations: `(let-linear ((x ...)) ...)`
- Compiler can then guarantee optimal deallocation

---

## Speculative Improvements

### A. Compile-Time Reference Counting Elimination

Idea: If we can prove at compile time that an object has refcount=1,
we can skip the refcount operations entirely.

```c
// Current
Obj* x = mk_int(10);
inc_ref(x);  // Maybe needed?
use(x);
dec_ref(x);  // Maybe frees?

// With uniqueness analysis
Obj* x = mk_int(10);  // Known unique
use(x);
free(x);  // Definitely frees
```

### B. Escape-Aware Allocation Strategy

Extend escape analysis to choose allocation strategy:

| Escape Class | Allocation | Deallocation |
|--------------|------------|--------------|
| ESCAPE_NONE  | Stack pool | Automatic (scope) |
| ESCAPE_ARG   | Arena/region | Region free |
| ESCAPE_GLOBAL| Heap | ASAP analysis |

### C. Interprocedural ASAP

Current limitation: analysis is intraprocedural.

**Improvement**: Summarize each function's memory behavior:
- Which parameters are consumed (freed)?
- Which parameters escape to return value?
- What new allocations are returned?

```c
// Function summary: consumes arg1, returns fresh
Obj* process(Obj* arg1 /*consumed*/, Obj* arg2 /*borrowed*/) {
  // ...
}

// At call site, compiler knows:
// - arg1 will be freed by callee, don't free here
// - arg2 must be freed by caller
// - result is fresh, caller must free
```

### D. Profile-Guided ASAP

Use runtime profiling to improve static analysis:
- Which branches are hot? Optimize free placement for those
- Which objects are actually long-lived? Adjust escape classification
- Which allocations are immediately freed? Candidates for stack/reuse

### E. Incremental Recompilation

When source changes, only re-analyze affected functions:
- Cache analysis results per function
- Invalidate only on signature changes
- Faster iteration for large codebases

---

## Solving the Cycle Problem (NO Garbage Collection)

ASAP's philosophy: **No stop-the-world GC, no heap scanning, fully deterministic**.

### Strategy Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    COMPILE-TIME ANALYSIS                         │
│                                                                  │
│  1. Shape Analysis: Tree / DAG / Potentially-Cyclic             │
│  2. Typed Reference Fields: Identify back-edge fields           │
│  3. Automatic Weak Insertion: Make back-edges weak              │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
       Tree/List             DAG            Cyclic Structure
          │                   │                   │
          ▼                   ▼                   ▼
     Pure ASAP           Refcount         Auto-Weak Back-Edges
   (immediate free)    (no cycle det)    OR Arena Allocation
```

---

### Solution 1: Typed Reference Fields (Compile-Time Weak Insertion)

From [Cyclic Reference Counting by Typed Reference Fields](https://www.sciencedirect.com/science/article/abs/pii/S1477842411000285):

**Key Insight**: In typed languages, we know the TYPE of each reference field.
We can analyze which fields could form back-edges (cycles) and automatically
make them weak.

```c
// Example: Doubly-linked list
struct Node {
    int value;
    Node* next;    // Forward edge - STRONG
    Node* prev;    // Back edge - AUTO-WEAK (compiler detects)
};

// Compiler analysis:
// - 'next' forms a chain: A.next → B.next → C.next → NULL (no cycle)
// - 'prev' points backward: C.prev → B.prev → A (forms cycle!)
// - Solution: Make 'prev' a weak reference automatically
```

**Algorithm**:
1. Build type graph of all struct/class definitions
2. For each reference field, determine if it could form a back-edge
3. Back-edge detection: field points to ancestor type in ownership hierarchy
4. Automatically treat back-edge fields as weak (don't prevent deallocation)

```
Type Analysis:
  Node.next : Node*  → Forward (same level) → STRONG
  Node.prev : Node*  → Backward (to parent) → WEAK
  Tree.left : Tree*  → Forward (child) → STRONG
  Tree.right: Tree*  → Forward (child) → STRONG
  Tree.parent: Tree* → Backward (to parent) → WEAK
```

**No programmer annotation needed** - compiler infers weak fields.

---

### Solution 2: CactusRef-Style Local Cycle Detection (Deterministic, Not GC)

From [CactusRef](https://github.com/artichoke/cactusref):

**Key Insight**: When dropping an object, do LOCAL reachability analysis
(not heap-wide scanning). This is O(cycle_size), not O(heap_size).

```c
// On drop of an object that MIGHT be in a cycle:
void drop_maybe_cyclic(Obj* obj) {
    obj->rc--;
    if (obj->rc == 0) {
        // Definitely garbage - free immediately
        free_children(obj);
        free(obj);
    } else if (obj->rc == obj->internal_links) {
        // All remaining refs are from within potential cycle
        // Do LOCAL BFS to check if cycle is orphaned
        if (is_orphaned_cycle(obj)) {
            free_cycle(obj);  // Deterministic, immediate
        }
    }
    // else: still externally referenced, don't free
}
```

**Why this is NOT garbage collection**:
- No heap scanning
- No stop-the-world pause
- O(cycle_size) not O(heap_size)
- Deterministic - happens at exact drop point
- Zero cost if you don't form cycles

---

### Solution 3: Arena Allocation for Complex Cyclic Structures

From [Arena Allocators](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator):

For truly complex cyclic graphs (e.g., arbitrary graph algorithms), use arenas:

```c
// Compiler detects: graph_build() creates cyclic structure
// Solution: Allocate from arena, free arena at scope end

void process_graph() {
    Arena* arena = arena_create();  // Bulk allocation region

    Graph* g = graph_build(arena);  // All nodes in same arena
    // Cycles within arena are fine - no individual tracking needed

    result = analyze(g);

    arena_destroy(arena);  // Bulk deallocation - O(1)
}
```

**When to use arenas** (compiler decides):
- Function creates cyclic structure that doesn't escape
- Graph algorithms
- Temporary complex data structures

---

### Solution 4: Shape Analysis → Pure ASAP for Most Code

From [Ghiya & Hendren](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-A-shape-for-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040):

Most data in most programs is **acyclic** (trees, lists, records).
Shape analysis proves this at compile time → pure ASAP, zero overhead.

```
Shape Analysis Results:
┌─────────────────┬──────────┬─────────────────────┐
│ Data Structure  │ Shape    │ Memory Strategy     │
├─────────────────┼──────────┼─────────────────────┤
│ Linked list     │ Tree     │ Pure ASAP           │
│ Binary tree     │ Tree     │ Pure ASAP           │
│ Hash map        │ DAG      │ Refcount (no cycle) │
│ Doubly-linked   │ Cyclic   │ Auto-weak back-edge │
│ Arbitrary graph │ Cyclic   │ Arena allocation    │
└─────────────────┴──────────┴─────────────────────┘
```

---

### Combined Algorithm

```
ASAP_analyze(program):
  for each type T in program:
    for each field F in T:
      if could_form_back_edge(T, F):
        mark_as_weak(F)

  for each allocation site A:
    shape = analyze_shape(A)
    if shape == Tree or shape == DAG:
      use_pure_asap(A)
    else if shape == Cyclic:
      if escapes_function(A):
        use_cactusref_style(A)  # Local cycle detection
      else:
        use_arena(A)  # Bulk deallocation
```

---

### Why This is NOT Garbage Collection

| Property | GC | ASAP Cycle Handling |
|----------|----|--------------------|
| Heap scanning | Yes | No |
| Stop-the-world | Yes | No |
| Non-deterministic timing | Yes | No |
| Runtime overhead always | Yes | No (zero for acyclic) |
| Complexity | O(heap) | O(cycle) or O(1) |

---

### References

- [Typed Reference Counting](https://www.sciencedirect.com/science/article/abs/pii/S1477842411000285) - Iowa State
- [CactusRef](https://github.com/artichoke/cactusref) - Deterministic cycle collection
- [Arena Allocators](https://www.rfleury.com/p/untangling-lifetimes-the-arena-allocator) - Ryan Fleury
- [Shape Analysis](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-A-shape-for-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040) - Ghiya & Hendren
- [Lobster Memory Management](https://aardappel.github.io/lobster/memory_management.html) - Compile-time RC

---

## Solutions to Remaining Research Questions

### 1. Exception Handling

**Problem**: When an exception is thrown, stack unwinding must free all live objects
to prevent memory leaks.

**Solution: LLVM-Style Landing Pads + Cleanup Clauses**

From [LLVM Exception Handling](https://llvm.org/docs/ExceptionHandling.html):

The compiler generates **landing pads** - alternative entry points for exception handling.
Each landing pad has **cleanup clauses** that specify what to free during unwinding.

```c
// ASAP compiler generates cleanup information:
void foo() {
    Obj* x = mk_int(10);  // Track: x needs cleanup
    Obj* y = mk_int(20);  // Track: y needs cleanup
    may_throw();          // If throws, cleanup x and y
    free_obj(y);          // Normal path
    free_obj(x);
}

// Generated exception table:
// Range [mk_int(10), may_throw): cleanup {x}
// Range [mk_int(20), may_throw): cleanup {x, y}
```

**Implementation for ASAP**:

1. **Track live allocations** at each program point
2. **Generate cleanup metadata** - list of objects to free at each potential throw point
3. **Emit landing pads** that call `free_obj()` for each live object
4. **Resume unwinding** after cleanup

```c
// Pseudocode for generated landing pad:
landing_pad:
    free_obj(y);  // Cleanup in reverse order
    free_obj(x);
    resume_unwind();
```

**Key Insight**: ASAP already knows which objects are live at each point (for CLEAN injection).
The same information drives exception cleanup tables.

**References**:
- [LLVM Exception Handling](https://llvm.org/docs/ExceptionHandling.html)
- [Stack Unwinding in C++](https://www.geeksforgeeks.org/cpp/stack-unwinding-in-c/)

---

### 2. Concurrency

**Problem**: Multiple threads accessing/freeing the same object causes data races.

**Solution: Ownership Transfer Semantics + Static Thread Analysis**

From [SOTER: Safe Ownership Transfer](https://experts.illinois.edu/en/publications/inferring-ownership-transfer-for-efficient-message-passing):

**Key Insight**: Many concurrent programs use **message passing** where ownership transfers
between threads. The sender gives up access, the receiver takes ownership.

```
Thread A                    Thread B
─────────                   ─────────
x = mk_obj()
send(channel, x)  ──────>   y = recv(channel)
// x is DEAD here           // y is LIVE here
// Don't free x             // Free y when done
```

**Implementation for ASAP**:

1. **Infer ownership transfer** at send/receive points
2. **Region-per-thread** for thread-local data (pure ASAP)
3. **Atomic refcounting** only for shared data (rare case)

```c
// Compiler classifies objects:
Obj* local = mk_int(10);      // Thread-local → pure ASAP
Obj* shared = mk_shared(20);  // Shared → atomic refcount
Obj* msg = mk_int(30);
send(chan, msg);              // Ownership transfer → no free here
```

**Rust's Approach** (for reference):

From [Rust Send/Sync traits](https://doc.rust-lang.org/book/ch16-04-extensible-concurrency-sync-and-send.html):

- `Send`: Type can be transferred to another thread (ownership transfer)
- `Sync`: Type can be shared between threads (requires synchronization)

ASAP can infer these properties without requiring explicit annotations.

**Erlang's Approach**:

From [Efficient Memory Management for Message-Passing](https://uu.diva-portal.org/smash/get/diva2:117278/FULLTEXT01.pdf):

- Private heaps per process (thread)
- Message area for data that will be sent
- Static analysis finds 99% of messages at compile time

**References**:
- [Inferring Ownership Transfer](https://experts.illinois.edu/en/publications/inferring-ownership-transfer-for-efficient-message-passing)
- [Proving Copyless Message Passing](https://link.springer.com/chapter/10.1007/978-3-642-10672-9_15)
- [Rust Send and Sync](https://doc.rust-lang.org/nomicon/send-and-sync.html)

---

### 3. Polymorphism (Generic Functions)

**Problem**: Generic functions don't know the concrete lifetimes of their type parameters.

```scheme
;; How does 'map' know when to free elements?
(define (map f xs)
  (if (null? xs)
      '()
      (cons (f (car xs)) (map f (cdr xs)))))
```

**Solution: Region Polymorphism (Tofte-Talpin)**

From [Region-Based Memory Management](https://en.wikipedia.org/wiki/Region-based_memory_management):

Functions are **region-polymorphic** - they abstract over the regions of their arguments.

```
map : ∀ρ1 ρ2 ρ3. (α →ρ1 β) → List α @ρ2 → List β @ρ3
```

The caller instantiates the region parameters:

```scheme
;; Caller knows: result goes in region r3, input from r2
(let ((result (map f input)))  ; r3 = local region
  (use result)
  ; free region r3 here
  )
```

**Implementation for ASAP**:

1. **Infer region parameters** for each function
2. **Monomorphize** when region is known at call site
3. **Pass region at runtime** when truly polymorphic

```c
// Monomorphized version (region known):
List* map_r1_r2(Fn* f, List* xs) {
    // Allocate result in region r2
    // Input from region r1
    // Compiler knows both regions' lifetimes
}

// Polymorphic version (region unknown):
List* map_poly(Region* r_out, Fn* f, List* xs) {
    // Allocate result in r_out
    // Caller manages r_out's lifetime
}
```

**Key Insight**: Most polymorphism is resolved at compile time through specialization.
Only higher-order functions with escaping closures need runtime region passing.

**References**:
- [Region-Based Memory Management - Tofte & Talpin](https://www.semanticscholar.org/paper/Region-based-Memory-Management-Tofte-Talpin/9117c75f62162b0bcf8e1ab91b7e25e0acc919a8)
- [Region-Based Memory Management in Cyclone](https://www.cs.umd.edu/projects/cyclone/papers/cyclone-regions.pdf)
- [MLKit Compiler](https://elsman.com/mlkit/)

---

### 4. Incremental Shape Analysis

**Problem**: Full shape analysis is expensive for large codebases. Re-analyzing
everything on each code change is impractical.

**Solution: Summary-Based Incremental Analysis**

From [SILVA (Scalable Incremental Layered Value-Flow Analysis)](https://dl.acm.org/doi/10.1145/3725214):

**Key techniques**:

1. **Function Summaries**: Compute shape information per-function, cache it
2. **Invalidation**: Only recompute when function signature changes
3. **Layered Analysis**: Start with fast imprecise analysis, refine incrementally

```
Code Change Detection:
  foo() modified → recompute foo's summary
  bar() calls foo → update bar's analysis using foo's new summary
  baz() unchanged → reuse cached analysis
```

**Performance** (from SILVA paper):
- 12× faster than full reanalysis for pointer analysis
- Handles changes up to 10K lines efficiently
- 31 seconds average for real-world commits (from SHARP paper)

**Implementation for ASAP**:

```
Per-Function Summary:
┌─────────────────────────────────────────┐
│ Function: process_list                   │
│ Parameters:                              │
│   xs: List @ρ1 (borrowed, not freed)    │
│ Returns: List @ρ2 (fresh allocation)    │
│ Shape: xs is Tree, result is Tree       │
│ Allocations: O(n) in ρ2                 │
│ Frees: none                             │
└─────────────────────────────────────────┘
```

When `process_list` changes:
1. Recompute its summary
2. Find all callers
3. Update callers' analysis using new summary
4. Propagate changes up call graph

**Dependency Tracking**:

```
Call Graph with Shape Dependencies:
  main() ──→ process_list() ──→ map()
     │              │              │
     │              │              └─ Summary: Tree→Tree
     │              └─ Summary: Tree→Tree (uses map's summary)
     └─ Uses process_list's summary
```

**References**:
- [SILVA: Scalable Incremental Analysis](https://dl.acm.org/doi/10.1145/3725214)
- [SHARP: Incremental Pointer Analysis](https://dl.acm.org/doi/10.1145/3527332)
- [Rethinking Incremental Pointer Analysis](https://dl.acm.org/doi/10.1145/3293606)

---

## Key References

1. [ASAP: As Static As Possible memory management](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf) - Proust, 2017
2. [Practical Static Memory Management](https://nathancorbyn.com/pdf/practical_static_memory_management.pdf) - Corbyn, 2020
3. [Perceus: Garbage Free Reference Counting with Reuse](https://dl.acm.org/doi/10.1145/3453483.3454032) - Reinking et al., PLDI 2021
4. [Destination-passing style for efficient memory management](https://dl.acm.org/doi/10.1145/3122948.3122949) - Shaikhha et al., 2017
5. [Better Static Memory Management](https://theory.stanford.edu/~aiken/publications/papers/pldi95.pdf) - Aiken et al., PLDI 1995
6. [Region-Based Memory Management](https://elsman.com/mlkit/) - Tofte & Talpin / MLKit
7. [MemFix: Static Analysis-Based Repair](https://dl.acm.org/doi/10.1145/3236024.3236079) - Lee et al., 2018
8. [A Lightweight Formalism for Rust Lifetimes](https://dl.acm.org/doi/10.1145/3443420) - ACM TOPLAS
