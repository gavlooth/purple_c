# Architecture

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

## Engine Constraints (Non‑Negotiable)
- **No stop‑the‑world GC**: no global traversal or global pauses.
- **Locality**: work is per‑object, per‑scope, or per‑subgraph.
- **Bounded Work**: deferred RC is capped per safe point; SCC computation is local to frozen graphs.
- **Explicit Registration**: external references are explicitly registered (e.g., arena externals).

## What “ASAP First” Means
- The compiler must remain correct **with only ASAP**.
- All other engines are optional accelerators or correctness patches for specific shapes.
- If an optimization fails or is disabled, the code should still be memory‑safe under ASAP.

## Files of Interest
- `src/eval/eval.c`: evaluator + codegen decisions.
- `src/analysis/*`: escape + shape analysis (ASAP decisions).
- `src/memory/*`: memory engines (SCC, deferred, arena, concurrent).
- `src/codegen/codegen.c`: runtime generation.

