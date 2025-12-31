# Changelog

All notable changes to Purple C Scratch are documented in this file.

## [Unreleased]

## [0.5.0] - 2025-12-31

### Added
- **Region References** (`src/memory/region.c`)
  - Vale/Ada/SPARK-style scope hierarchy validation
  - Key invariant: pointer cannot point to more deeply scoped region
  - Prevents cross-scope dangling references at link time
  - O(1) depth check on reference creation
  - API: `region_context_new()`, `region_enter()`, `region_exit()`,
         `region_alloc()`, `region_create_ref()`, `region_can_reference()`

- **Random Generational References** (`src/memory/genref.c`)
  - Vale-style use-after-free detection
  - Each object has random 64-bit generation number
  - Each reference remembers generation at creation time
  - On deref: if gen mismatch → UAF detected (O(1) check)
  - On free: gen = 0 (invalidates all existing references)
  - Closure capture validation before execution
  - API: `genref_alloc()`, `genref_create_ref()`, `genref_deref()`,
         `genref_is_valid()`, `genref_closure_*`

- **Constraint References** (`src/memory/constraint.c`)
  - Assertion-based safety for complex patterns (graphs, observers, callbacks)
  - Single owner + multiple non-owning "constraint" references
  - On free: ASSERT constraint count is zero (with `CONSTRAINT_ASSERT` define)
  - Catches dangling references at development time
  - API: `constraint_alloc()`, `constraint_add()`, `constraint_release()`,
         `constraint_free()`, `constraint_get_stats()`

- **Tiered Safety Strategy**
  - Simple (90%): Pure ASAP + ad-hoc validation (zero cost)
  - Cross-scope (7%): Region refs (+8 bytes, O(1) check)
  - Closures/callbacks (3%): Random gen refs (+16 bytes, 1 cmp)
  - Debug mode: + Constraint refs (assert on free)

### Changed
- Updated Makefile to include `region.c`, `genref.c`, `constraint.c`

### References
- Vale Grimoire: https://verdagon.dev/grimoire/grimoire
- Random Generational References: https://verdagon.dev/blog/generational-references
- Ada/SPARK scope rules for pointer safety

## [0.4.0] - 2025-12-31

### Added
- **Symmetric Reference Counting** (`src/memory/symmetric.c`)
  - New hybrid memory strategy for unbroken cycles
  - Treats scope as object participating in ownership graph
  - External refs (from scopes) vs Internal refs (from objects)
  - O(1) deterministic cycle collection without global GC
  - More memory efficient than arenas for long-running scopes
  - API: `sym_context_new()`, `sym_enter_scope()`, `sym_exit_scope()`,
         `sym_alloc()`, `sym_link()`, `sym_is_orphaned()`

- **RC Operation Elimination** (`src/analysis/rcopt.c`)
  - Lobster-style compile-time RC optimization
  - **Uniqueness Analysis**: Prove single reference → use `free_unique()`
  - **Alias Tracking**: Track variable aliases → elide redundant RC ops
  - **Borrow Tracking**: Parameters without ownership → zero RC operations
  - API: `rcopt_define_var()`, `rcopt_define_alias()`, `rcopt_define_borrowed()`,
         `rcopt_get_free_function()`, `rcopt_get_stats()`

- **`free_unique()` Runtime Function**
  - Direct free for proven-unique references
  - Skips reference count check at runtime
  - Generated in codegen output

- **Hybrid Memory Strategy**
  - TREE → `free_tree()` (zero overhead)
  - DAG → `dec_ref()` (standard RC)
  - CYCLIC + broken → `dec_ref()` (weak edges)
  - CYCLIC + frozen → `scc_release()` (SCC-based)
  - CYCLIC + unbroken → `symmetric_rc` (NEW default)
  - Arena available as opt-in for batch operations

### Changed
- Updated Makefile to include `rcopt.c` and `symmetric.c`
- Generated runtime now includes Symmetric RC functions

### References
- Lobster Memory Management: https://aardappel.github.io/lobster/memory_management.html

## [0.3.0] - 2025-12-31

### Added
- **Three-Phase Automatic Back-Edge Detection** (`src/codegen/codegen.c`)
  - Phase 1: Naming heuristics (`parent`, `prev`, `owner`, `back`, etc.)
  - Phase 2: Second-pointer detection (multiple pointers to same type)
  - Phase 3: DFS cycle detection for remaining edges
  - No programmer annotation required
  - Ported from Go implementation

### Changed
- `analyze_back_edges()` now uses three-phase approach
- Updated ARCHITECTURE.md with algorithm documentation

## [0.2.0] - 2025-12-31

### Added
- **Integrated Memory Management Runtime**
  - Arena with externals support
  - SCC-based RC (Tarjan's algorithm)
  - Deferred RC with bounded processing
  - Perceus reuse analysis
  - All strategies in single generated runtime

- **Type Registry with Back-Edge Detection**
  - Automatic weak field detection
  - Type-aware release functions
  - Field accessors generation

- **Concurrency Support**
  - Thread-local storage
  - Atomic reference counting
  - Message channels with ownership transfer
  - Thread spawn with ownership semantics

- **Exception Handling**
  - Exception frames with cleanup
  - Try/catch macros
  - Cleanup registration

### Fixed
- Weak reference invalidation function naming
- Various memory safety improvements

## [0.1.0] - 2025-12-30

### Added
- Initial implementation
- Stage-polymorphic evaluator
- S-expression parser
- ASAP memory management
- Shape analysis (Ghiya-Hendren)
- Escape analysis
- Basic code generation

### References
- ASAP: Proust, "As Static As Possible memory management" (2017)
- Shape Analysis: Ghiya & Hendren, POPL 1996
- Perceus: Reinking et al., PLDI 2021
- Region-Based Memory: Tofte & Talpin, 1997
