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
- `src/analysis/*`: escape + shape analysis (ASAP decisions).
- `src/memory/*`: memory engines (SCC, deferred, arena, concurrent).
- `src/codegen/codegen.c`: runtime generation, type registry, back-edge detection.

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


## Revision
Relax the "ASAP First a litle but keep the no stop the world"
- Implement arena + weak edges in an ASAP-style compiler

This proposal extends ASAP in a practical, conservative, and sound way by:

- embracing bulk deallocation via arenas

- allowing cycles without lifetime inference

- preserving static determinism


