# Architecture

## Core Invariants

These are the non-negotiable principles of Purple C's memory management:

| # | Invariant | Implication |
|---|-----------|-------------|
| 1 | **No stop-the-world GC is the central invariant** | No global heap traversals, no pauses, ever. |
| 2 | **ASAP is the foundation, not the entire solution** | Compile-time free insertion is the baseline; RC, arenas, weak refs layer on top. |
| 3 | **Reference counting is acceptable and expected** | RC is the right tool for DAGs and shared data—don't avoid it. |
| 4 | **Cycles are handled incrementally and locally** | Per-object or per-subgraph work only; O(cycle), not O(heap). |
| 5 | **Correctness is preserved even when optimizations are disabled** | With only ASAP enabled, the system must still be memory-safe. |

---

## Core Idea
Purple C is a **stage‑polymorphic evaluator**: the same evaluator interprets concrete values and compiles lifted values to C. Memory management is **ASAP first** (compile‑time free insertion), and every runtime optimization sits **on top of** that baseline. The system explicitly forbids stop‑the‑world GC.

## Layers (High‑Level)
1) **Evaluator / Codegen**
   - `eval` returns either concrete values or `Code` values.
   - Codegen emits a standalone C runtime plus the compiled expression.

2) **ASAP (Base Engine)**
   - Compile‑time liveness + shape analysis decides *where* and *how* to free.
   - Produces `free_tree`, `dec_ref`, `deferred_release`, etc.

3) **Optimizations on Top of ASAP**
   - **Shape Analysis**: TREE/DAG/CYCLIC strategies.
   - **DAG RC**: `inc_ref`/`dec_ref` for shared acyclic graphs.
   - **SCC RC** (frozen cycles): Tarjan on a *local* subgraph; no global pause.
   - **Deferred RC** (mutable cycles): bounded O(k) work at safe points.
   - **Weak Refs**: explicit invalidation on free.
   - **Perceus Reuse**: reuse eligible objects (no global scans).
   - **Arena Scopes**: bulk allocation/free for cyclic data that does not escape.
   - **Concurrency**: ownership transfer + atomic RC for shared data.
   - **Exceptions / DPS**: cleanup is localized to tracked live objects.

## Engine Constraints (Derived from Core Invariants)
- **No stop‑the‑world GC** (Invariant 1): no global traversal or global pauses.
- **Locality** (Invariant 4): work is per‑object, per‑scope, or per‑subgraph.
- **Bounded Work** (Invariant 4): deferred RC is capped per safe point; SCC computation is local to frozen graphs.
- **Explicit Registration**: external references are explicitly registered (e.g., arena externals).
- **RC is first-class** (Invariant 3): don't contort the design to avoid reference counting.

## What "ASAP as Foundation" Means (Invariants 2 & 5)
- The compiler must remain correct **with only ASAP** — all optimizations can be disabled.
- RC, arenas, weak refs, SCC freezing are **layered on top**, not replacements.
- If an optimization fails or is disabled, the code is still memory‑safe under ASAP alone.

## Files of Interest
- `src/eval/eval.c`: evaluator + codegen decisions.
- `src/analysis/*`: escape + shape analysis, RC optimization (ASAP decisions).
- `src/memory/*`: memory engines (SCC, deferred, arena, symmetric, concurrent).
- `src/codegen/codegen.c`: runtime generation, type registry, back-edge detection.

## Hybrid Memory Strategy (v0.4.0)

The compiler uses a **hybrid strategy** selecting optimal memory management per allocation:

```
┌─────────────────────────────────────────────────────────────────┐
│                    COMPILE-TIME ANALYSIS                         │
│                                                                  │
│  Shape Analysis ──► TREE / DAG / CYCLIC                         │
│  Back-Edge Detection ──► cycles_broken: true/false              │
│  Escape Analysis ──► local / arg / global                       │
│  RC Optimization ──► unique / alias / borrowed                  │
└─────────────────────────────────────────────────────────────────┘
                              │
          ┌───────────────────┼───────────────────┐
          ▼                   ▼                   ▼
       TREE/DAG          CYCLIC+broken      CYCLIC+unbroken
          │                   │                   │
          ▼                   ▼                   ▼
   Pure ASAP/RC          dec_ref           Symmetric RC
   (free_tree/dec_ref)   (weak edges)      (scope-as-object)
```

### Strategy Selection

| Shape | Cycle Status | Frozen | Strategy | Function |
|-------|--------------|--------|----------|----------|
| TREE | - | - | ASAP | `free_tree()` |
| DAG | - | - | Standard RC | `dec_ref()` |
| CYCLIC | Broken | - | Standard RC | `dec_ref()` |
| CYCLIC | Unbroken | Yes | SCC-based RC | `release_scc()` |
| CYCLIC | Unbroken | No | **Symmetric RC** | `sym_exit_scope()` |
| Unknown | - | - | Symmetric RC | `sym_exit_scope()` |

### RC Optimization Layer

Lobster-style compile-time RC elimination (~75% ops removed):

| Optimization | Description | Result |
|--------------|-------------|--------|
| Uniqueness | Proven single ref | `free_unique()` |
| Aliasing | Multiple vars, same object | Only one RC |
| Borrowing | Parameters, temps | Zero RC ops |

## Three-Phase Automatic Back-Edge Detection

The compiler automatically detects back-edges in cyclic data structures to break ownership cycles. No programmer annotation required.

### Phase 1: Naming Heuristics
Fields with names suggesting back-references are automatically marked weak:
- `parent`, `owner`, `container` - points to ancestor/owner
- `prev`, `previous`, `back` - reverse direction in sequences
- `up`, `outer` - hierarchical back-references

### Phase 2: Second-Pointer Detection
If a type has multiple pointers to the same target type, and no weak pointer already exists, the second pointer is marked weak:
```c
struct Node {
    Node* next;   // STRONG (first)
    Node* prev;   // WEAK (second, if not caught by naming)
}
```

### Phase 3: DFS Cycle Detection
For remaining cycles not caught by heuristics:
- DFS traversal of the type graph
- Only marks edges as back-edges if the cycle isn't already broken
- Prevents over-marking that would weaken more fields than necessary

### Result
Cycles are broken with minimal weak references, enabling safe `dec_ref` without cycle-collection overhead.


## RC Optimization (Lobster-style)

Based on the Lobster language's compile-time RC optimization, which eliminates ~95% of reference counting operations through static analysis.

### Key Optimizations

1. **Uniqueness Analysis**: Prove when a reference is the only one to an object
   - Fresh allocations start as unique
   - Aliasing marks variables as non-unique
   - Unique references use `free_unique()` instead of `dec_ref()`

2. **Alias Tracking**: Track when multiple variables point to the same object
   - Only one alias needs to perform RC operations
   - Other aliases elide their RC operations

3. **Borrow Tracking**: Parameters and temporary references without ownership
   - Borrowed references never perform RC operations
   - No ownership transfer occurs

### Implementation

Located in `src/analysis/rcopt.c`:
- `RCOptContext`: Holds optimization state
- `rcopt_define_var()`: Define unique variable
- `rcopt_define_alias()`: Define alias
- `rcopt_define_borrowed()`: Define borrowed reference
- `rcopt_get_free_function()`: Get optimized free function

### Generated Code

```c
// Before RC optimization:
Obj* x = mk_int(10);
dec_ref(x);  // Runtime RC check

// After RC optimization (proven unique):
Obj* x = mk_int(10);
free_unique(x);  // No RC check needed
```

### References
- Lobster: https://aardappel.github.io/lobster/memory_management.html


## Symmetric Reference Counting (Hybrid Strategy)

The **default for unbroken cycles** - more memory efficient than arenas.

### Key Insight
Treat scope as an object that participates in the ownership graph:
- **External refs**: From live scopes/roots
- **Internal refs**: Within the object graph
- **When external_rc drops to 0**, the cycle is orphaned → freed immediately

### Why It's Better Than Arenas for Cycles

| Aspect | Arena | Symmetric RC |
|--------|-------|--------------|
| Peak memory | Higher (holds until scope end) | Lower (immediate free) |
| Long-running scopes | Holds everything | Frees as you go |
| Memory efficiency | O(scope_lifetime) | O(object_lifetime) |

### Implementation
Located in `src/memory/symmetric.c`:
- `sym_enter_scope()`: Enter new scope
- `sym_exit_scope()`: Exit scope, release owned objects, collect cycles
- `sym_alloc()`: Allocate object owned by current scope
- `sym_link()`: Create internal reference between objects

### Generated Code

```c
// Symmetric RC handles cycles automatically:
sym_enter_scope();
SymObj* a = sym_alloc(mk_int(1));
SymObj* b = sym_alloc(mk_int(2));
sym_link(a, b);  // A → B
sym_link(b, a);  // B → A (cycle!)
sym_exit_scope();  // Both freed - cycle collected!
```


## Revision
Relax the "ASAP First a litle but keep the no stop the world"
- Implement arena + weak edges in an ASAP-style compiler

This proposal extends ASAP in a practical, conservative, and sound way by:

- embracing bulk deallocation via arenas

- allowing cycles without lifetime inference

- preserving static determinism


