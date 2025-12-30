# FIXME Tracker (Generated from review)

Break tests live in `tests_break.sh` (and repro helpers in `tests/` and `run_repro.sh`).
Items below are grouped by status.

## Open (break tests failing)

- None

## Open (no failing break test yet)

- None

## Resolved (break tests passing)

24) **[Parser]** Stack overflow on deep nesting (recursion in `parse`/`parse_list`)
    - Fix: Implemented iterative parser with explicit stack

25) **[Runtime]** Stack overflow in generated Tarjan runtime
    - Fix: Generated iterative `tarjan_strongconnect` using `TarjanWorkFrame`

26) **[Compiler]** Buffer overflow in `analyze_back_edges` (fixed `path[256]`)
    - Fix: Replaced fixed array with dynamic `realloc` array

27) **[Legacy]** Buffer overflows in `main.c`
    - Fix: Replaced fixed buffers with dynamic reading from stdin/args

1) Invalid C codegen for `if` expressions
   - Test: `IfCodegenC`

19) **[Memory]** Interpreter/Compiler leaks all allocated values during execution
    - Fix: Added Phase 12 compiler arena - bulk deallocation at end of main()

20) **[Performance]** Quadratic O(N^2) in generated Tarjan/deferred runtime (linear scans)
    - Fix: Added hash maps to generated Tarjan and deferred runtimes for O(1) lookups

21) **[Concurrency]** Use-After-Free when sending objects via channels (move vs local dec)
    - Fix: channel_send now increments RC before transfer for safe sender cleanup

22) **[Shape]** Unsafe `free_tree` when mutation/escape info is unknown
    - Fix: `shape_free_strategy` now defaults to `dec_ref` for SHAPE_UNKNOWN

23) **[Reuse]** `try_reuse` overwrites fields without releasing existing pointers
    - Fix: `try_reuse` now releases children before reusing object

2) Cyclic free function mismatch (`deferred_release` missing)  
   - Test: `DeferredReleaseExists`

3) Tarjan traversal lacked `is_pair` guard  
   - Test: `TarjanGuardsIsPair`

4) Deferred RC lacked `is_pair` guard  
   - Test: `DeferredGuardsIsPair`

5) DAG refcounting missing `inc_ref`  
   - Test: `IncRefExists`

6) Weak references not invalidated on release  
   - Test: `WeakInvalidateOnRelease`

7) `free_obj` lacked dedup/guard against pointer reuse  
   - Test: `FreeObjGuardsReuse`

8) SCC runtime did not link SCCs into result list  
   - Test: `SccRuntimeLinksResult`

9) Return-escaped values freed at end of `let`  
   - Test: `EscapeReturnNoFree`

10) Shape analysis ignored `letrec` cycles  
    - Test: `LetrecShapeCyclic` (unit)

11) SCC recomputation after mutation returned stale results  
    - Test: `SccRecomputeAfterMutation` (unit)

12) Immutable RC path was a no-op (leaks for shared frozen)  
    - Test: `ImmutableRcIsNoop`

13) `conc_freeze` lacked guard for already-frozen nodes  
    - Test: `ConcFreezeHasGuard`

14) `scan_*` mutated `mark` (RC) instead of a separate scan tag  
    - Test: `ScanDoesNotTouchMark`

15) SCC runtime used fixed-size arrays without bounds checks  
    - Tests: `SccRuntimeNoFixedArrays`, `SccRegistryNoFixedArrays`

16) Fixed-size buffer overflow risk in `list_to_str`  
    - Test: `ListToStrLarge` (unit)

17) Deeply nested parse regression (stack overflow risk)  
    - Test: `ParseDeepNesting` (unit)

18) **[Arena]** External heap references are leaked when an Arena is destroyed  
    - Test: `ArenaReleasesExternal` (unit)
