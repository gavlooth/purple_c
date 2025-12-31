# Changelog

All notable changes to Purple C Scratch are documented in this file.

## [Unreleased]

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
