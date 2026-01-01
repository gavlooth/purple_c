# FIXME Tracker (Generated from review)

Break tests live in `tests_break.sh` (and repro helpers in `tests/` and `run_repro.sh`).
Items below are grouped by status.

## Open (break tests failing)

- None

## Open (no failing break test yet)

- None

## Resolved (break tests passing)

1) Invalid C codegen for `if` expressions
   - Test: `IfCodegenC`

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

24) **[Parser]** Stack overflow on deep nesting (recursion in `parse`/`parse_list`)
    - Fix: Implemented iterative parser with explicit stack

25) **[Runtime]** Stack overflow in generated Tarjan runtime
    - Fix: Generated iterative `tarjan_strongconnect` using `TarjanWorkFrame`

26) **[Compiler]** Buffer overflow in `analyze_back_edges` (fixed `path[256]`)
    - Fix: Replaced fixed array with dynamic `realloc` array

27) **[Legacy]** Buffer overflows in `main.c`
    - Fix: Replaced fixed buffers with dynamic reading from stdin/args

28) **[Concurrent]** Unchecked `malloc` in `concurrent.c` (host & generated)
    - Fix: Added NULL checks in generated runtime (`conc_mk_int`, `conc_mk_pair`, `channel_create`, `spawn_thread`) and channel guard

29) **[Arena]** Unchecked `malloc` in `arena_register_external` (host & generated)
    - Fix: Added NULL checks in host and generated runtime

30) **[SCC]** Unchecked `malloc`/`realloc` in generated SCC runtime
    - Fix: Added TARJAN_OOM flag and allocation guards in generated Tarjan

31) **[HashMap]** Silent failure on allocation
    - Fix: Track allocation failures via `had_alloc_failure` flag

32) **[Deferred]** Silent failure in `defer_decrement` (host & generated)
    - Fix: Track `dropped_decrements` and add immediate decrement fallback in generated runtime

33) **[Compiler]** Silent failure in `h_let_default` allocation
    - Fix: Abort `let` with an error on OOM

34) **[Compiler]** Invalid C codegen for complex literals in `let`
    - Fix: Emit C literals via `val_to_c_expr`; unsupported literals fall back to interpreter/error

35) **[Compiler]** `letrec` value placeholder semantics
    - Fix: Use uninitialized sentinel; error on access before initialization

36) **[Memory]** Silent failure in `compiler_arena_register_string`
    - Fix: Allocate tracking nodes in the arena and warn on OOM

37) **[Arena]** Compiler arena never actually used, so Values leak
    - Fix: Allocate an initial arena block in `compiler_arena_init`

38) **[ASAP Scanner]** Generated list scanner can recurse into non-pairs (undefined behavior)
    - Fix: Add `is_pair` guard for `scan_List` and `clear_marks_List`

39) **[Weak/Scanner]** Field-aware scanner traverses weak fields with the wrong type
    - Fix: Skip weak fields in scanner generation

40) **[Concurrency]** Channel send/recv can deadlock after `pthread_cond_wait`
    - Fix: Reload `head`/`tail` after each wait

41) **[Runtime]** Pointer-range checks in `free_tree`/`dec_ref` are UB
    - Fix: Use `uintptr_t`-based `is_stack_obj` helper

42) **[Codegen]** Mixed code/non-code non-int values can emit invalid C
    - Fix: Use `val_to_c_expr` in `emit_c_call`/`let` and error on unsupported literals

43) **[Symbols]** `mk_sym(NULL)` produces values that crash `sym_eq`/`val_to_str`
    - Fix: Coerce NULL to empty string in `mk_sym`/`mk_code` and guard comparisons

44) **[Shape]** NULL dereference in `analyze_shapes_expr` when `car(expr)` returns NULL
    - Fix: Added NULL check before accessing `op->tag` in shape.c

45) **[Shape]** NULL dereference when accessing `sym->s` in let/letrec bindings
    - Fix: Added `sym && sym->tag == T_SYM` guards before accessing `sym->s`

46) **[SCC]** NULL dereference in `has_no_mutations` when `car(expr)` returns NULL
    - Fix: Added NULL check before accessing `op->tag` in scc.c

47) **[SCC]** NULL dereference when accessing `target->tag` in `has_no_mutations`
    - Fix: Added NULL check before accessing `target->tag`

48) **[Codegen]** NULL dereference in `lift_value` when called with NULL
    - Fix: Added NULL check at start of `lift_value`

49) **[SCC Registry]** Registry corruption when Tarjan overwrites `scc->next`
    - Fix: Added separate `result_next` field to SCC struct for result list

50) **[SCC Release]** Generated `release_scc` doesn't invalidate weak refs before freeing
    - Fix: Added `invalidate_weak_refs_for` call before freeing SCC members

51) **[Main]** Integer overflow in stdin buffer resizing
    - Fix: Added overflow check before doubling capacity

52) **[Main]** Unescaped input in comments
    - Fix: Added `escape_for_comment()` helper that escapes newlines/tabs/control chars before printing in C comments

53) **[Codegen]** `prim_null` generated `is_nil(x)` returning `int` instead of `Obj*`
    - Fix: Changed `prim_null` to emit `mk_int(is_nil(x))` so the result is an `Obj*`
    - Test: `NullLiftCodegen` in tests_break.sh

54) **[Concurrent]** Division by zero in generated `channel_create` when capacity is 0
    - Fix: Added `if (capacity <= 0) return NULL` guard
    - Test: `Phase11-ChannelZeroCap` in tests.sh

55) **[Parser]** Unchecked `strndup` return value creates empty symbols on OOM
    - Fix: Added NULL check after `strndup`; returns NULL and cleans up on failure
    - Note: OOM is difficult to unit test; fix is defensive

56) **[Deftype]** Unchecked `strdup` in user type registration causes NULL dereference
    - Fix: Added NULL checks after all `strdup` calls in `user_register_type`, `user_add_type_field`
    - Fix: Added defensive NULL check in `user_find_type` before `strcmp`
    - Fix: Added proper error cleanup paths in `eval_deftype` for OOM
    - Test: `Deftype-NullGuard` in tests.sh

57) **[Eval]** Unchecked `mk_menv` return value causes NULL dereference on OOM
    - Fix: Added NULL checks after all `mk_menv` calls in:
      - `h_app_default` (function application)
      - `h_let_default` (let binding - two locations)
      - `prim_call_cc` (continuation creation)
      - `prim_select` (select statement recv case)
      - `eval` (EM meta-level creation)
      - `main` (initial menv creation)
    - Tests: `MkMenv-FunctionApp`, `MkMenv-LetBinding` in tests.sh
