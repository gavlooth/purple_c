# ASAP Memory Management - Project TODO

## Purpose & Core Principles

**Goal**: Implement a memory management system that achieves:

1. **No Stop-the-World Garbage Collection**
   - All deallocation is determined at compile time
   - Runtime memory management is fully deterministic
   - No heap traversal to find garbage
   - No pause times, no GC latency spikes

2. **Maximum Performance**
   - Compile-time analysis enables optimal deallocation strategy per variable
   - Shape analysis (Tree/DAG/Cyclic) selects fastest free method
   - Drop-guided reuse avoids allocation when possible
   - Non-lexical lifetimes free memory at earliest safe point
   - Stack allocation for non-escaping values

3. **No Language Restrictions**
   - No linear types required from programmer
   - No ownership annotations needed
   - No borrow checker syntax
   - No lifetime parameters in user code
   - Programmer writes standard Purple/Lisp code
   - All memory decisions are automatic and transparent

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

| Data Type | Detection | Strategy | Pause | Proven |
|-----------|-----------|----------|-------|--------|
| TREE (no sharing) | Shape analysis | `free_tree()` - direct recursive free | Zero | Yes |
| DAG (sharing, no cycles) | Shape analysis | `dec_ref()` - simple refcount | Zero | Yes |
| Cyclic + Frozen | Freeze detection | SCC-level RC (ISMM 2024) | Zero | Yes |
| Cyclic + Mutable | Fallback | Deferred list + bounded processing | Zero | Yes |

### Key Techniques

#### 1. ASAP (As Static As Possible)
- **Source**: [UCAM-CL-TR-908](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf)
- **Role**: Primary strategy for acyclic data (majority of cases)
- **How**: Compile-time analysis inserts `free` calls at optimal points
- **Handles**: TREE and DAG structures (most functional data)

#### 2. ISMM 2024 - Deeply Immutable Cycles
- **Source**: [Reference Counting Deeply Immutable Data Structures with Cycles](https://dl.acm.org/doi/10.1145/3652024.3665507)
- **Role**: Primary strategy for cyclic data after freeze
- **How**:
  1. Detect "freeze point" where cyclic structure becomes immutable
  2. Compute SCCs once at freeze time (almost linear)
  3. Single reference count per SCC, not per object
- **Handles**: Cyclic structures that become immutable
- **Guarantee**: Precise reachability, deterministic reclamation, no backup collector

#### 3. Deferred RC Fallback
- **Role**: Edge case handler for mutable cycles that never freeze
- **How**:
  1. Zero-count objects added to deferred list
  2. Bounded processing at safe points (function boundaries)
  3. O(k) work per safe point, k is constant
- **Handles**: Rare mutable cyclic data
- **Guarantee**: Zero-pause (bounded work), eventually freed

### Invariants

1. **Soundness**: All strategies are proven correct
2. **Zero-pause**: O(k) bounded work, no heap traversal
3. **Deterministic**: All reclamation is predictable
4. **Complete**: Every object is eventually freed
5. **No restrictions**: Programmer writes normal code

---

## Implementation Status

### Phase 1: Core Infrastructure ✅
- [x] VarUsage and AnalysisContext data structures
- [x] Liveness analysis
- [x] Escape analysis (ESCAPE_NONE, ESCAPE_ARG, ESCAPE_GLOBAL)
- [x] Capture tracking for closures
- [x] Dynamic free list
- [x] Multi-binding let support

### Phase 2: Shape Analysis (ASAP) ✅
- [x] Define Shape enum (TREE, DAG, CYCLIC)
- [x] Implement intraprocedural shape analysis
- [x] Alias group tracking
- [x] Integrate with code generation
- [x] Generate shape-specific free functions (free_tree, dec_ref)

### Phase 3: Typed Reference Fields ✅
- [x] Define type/struct registry with FieldStrength
- [x] Build ownership graph from type definitions
- [x] Detect back-edge fields via DFS cycle detection
- [x] Generate field-aware code

### Phase 4: Drop-Guided Reuse ✅
- [x] Track allocation sites (AllocSite)
- [x] Track free sites (FreeSite)
- [x] Match free/alloc pairs of same size
- [x] Generate reuse-aware constructors (try_reuse, reuse_as_int, reuse_as_pair)

### Phase 5: Non-Lexical Lifetimes ✅
- [x] Build Control Flow Graph (CFG)
- [x] Implement dataflow liveness analysis
- [x] Find earliest free point per variable
- [x] Support conditional frees on different paths

### Phase 6: Freeze Detection & SCC-based RC (ISMM 2024) ✅
- [x] Detect freeze points (where data becomes immutable)
- [x] Implement Tarjan's SCC algorithm for frozen structures
- [x] Generate SCC-level reference counting
- [x] Handle external references to SCC members

### Phase 7: Deferred RC Fallback ✅
- [x] Implement deferred list for zero-count objects
- [x] Add safe points at function boundaries
- [x] Bounded processing (O(k) per safe point)
- [x] Root scanning for mutable cycle detection

### Phase 8: Arena Allocation ✅
- [x] Implement arena allocator runtime
- [x] Arena-aware constructors (arena_mk_int, arena_mk_pair)
- [x] O(1) bulk deallocation via arena_destroy
- [x] Scope detection for arena-eligible allocations

### Phase 9: Destination-Passing Style (Optimization) ✅
- [x] Identify DPS candidates (functions returning fresh allocations)
- [x] Transform function signatures to take destination pointer
- [x] Stack-allocate destinations where possible
- [x] DPS runtime (Dest struct, write_int, write_pair, add_dps, map_dps, fold_dps)

### Phase 10: Exception Handling 🔲
- [ ] Track live allocations at each point
- [ ] Generate landing pads for cleanup
- [ ] Handle nested try/catch

### Phase 11: Concurrency Support 🔲
- [ ] Detect thread spawn points
- [ ] Infer ownership transfer automatically
- [ ] Use concurrent deferred RC (PLDI 2021) if needed

---

## Test Status

All 44 tests passing:
- 13 original ASAP tests
- 8 optimization tests (Phases 2-5)
- 4 Phase 6b tests (ISMM 2024 SCC-based RC)
- 4 Phase 7 tests (Deferred RC)
- 6 Phase 8 tests (Arena Allocation)
- 7 Phase 9 tests (DPS)
- 2 integration tests

---

## Research Foundation

### Primary Strategy Papers

| Paper | Year | Technique | Role |
|-------|------|-----------|------|
| [ASAP](https://www.cl.cam.ac.uk/techreports/UCAM-CL-TR-908.pdf) | 2020 | Compile-time free insertion | Acyclic data |
| [Deeply Immutable Cycles](https://dl.acm.org/doi/10.1145/3652024.3665507) | 2024 | SCC-based RC for frozen cycles | Cyclic data |

### Optimization Papers

| Paper | Year | Technique | Application |
|-------|------|-----------|-------------|
| [Drop-Guided Reuse](https://dl.acm.org/doi/10.1145/3547634) | 2022 | Frame-limited reuse | Memory reuse |
| [Concurrent Deferred RC](https://dl.acm.org/doi/10.1145/3453483.3454060) | 2021 | Constant-time concurrent RC | Concurrency |
| [CIRC](https://dl.acm.org/doi/10.1145/3656383) | 2024 | Immediate reclaim, no growth | Concurrency |
| [Shape Analysis](https://www.semanticscholar.org/paper/Is-it-a-tree,-a-DAG,-or-a-cyclic-graph-Ghiya-Hendren/115be3be1d6df75ff4defe0d7810ca6e45402040) | 1996 | Tree/DAG/Cyclic classification | Optimization |

### Fallback Papers

| Paper | Year | Technique | Application |
|-------|------|-----------|-------------|
| [Ulterior RC](https://www.cs.utexas.edu/~mckinley/papers/urc-oopsla-2003.pdf) | 2003 | Generational deferred RC | Fallback |
| [Deferred Decrementing](https://www.memorymanagement.org/mmref/recycle.html) | - | Bounded pause processing | Fallback |

---

## Design Decisions

### Why ASAP + ISMM 2024?

| Concern | Solution |
|---------|----------|
| Acyclic data | ASAP handles perfectly (compile-time) |
| Cyclic data | ISMM 2024 handles after freeze (SCC RC) |
| Zero-pause | Both techniques are zero-pause |
| No restrictions | Programmer writes normal code |
| Proven | Both techniques have formal proofs |

### What About Mutable Cycles?

Mutable cycles that never freeze are handled by deferred RC fallback:
- Rare in practice (most cyclic data eventually becomes immutable)
- Still zero-pause (bounded processing at safe points)
- Still correct (just slightly delayed reclamation)

### Rejected Alternatives

| Alternative | Why Rejected |
|-------------|--------------|
| Ownership types | Restricts language (violates goal #3) |
| Runtime cycle collector | Causes pauses (violates goal #1) |
| Weak references everywhere | User-visible semantics, error-prone |

---

## Key Files

| File | Description |
|------|-------------|
| `src/main.c` | Entry point and initialization |
| `src/types.h/c` | Core Value types and constructors |
| `src/analysis/escape.h/c` | Escape analysis & capture tracking |
| `src/analysis/shape.h/c` | Shape analysis (TREE/DAG/CYCLIC) |
| `src/memory/scc.h/c` | Phase 6b: SCC-based RC (ISMM 2024) |
| `src/memory/deferred.h/c` | Phase 7: Deferred RC fallback |
| `src/memory/arena.h/c` | Phase 8: Arena allocator |
| `src/analysis/dps.h/c` | Phase 9: Destination-Passing Style |
| `src/codegen/codegen.h/c` | Code generation & runtime emission |
| `src/eval/eval.h/c` | Evaluator & handlers |
| `src/parser/parser.h/c` | Reader/parser |
| `main.c` | Legacy monolithic implementation |
| `tests.sh` | Test suite (44 tests) |
| `Makefile` | Build system for modular structure |
| `IMPLEMENTATION_PLAN.md` | Detailed implementation plan with pseudocode |
| `CLAUDE.md` | Documentation of ASAP principles |

---

## Next Steps

1. **Implement Phase 10**: Exception handling with landing pads
2. **Implement Phase 11**: Concurrency support with ownership transfer
3. **Benchmark**: Compare performance vs tracing GC
4. **Real-world testing**: Test with larger Purple programs
