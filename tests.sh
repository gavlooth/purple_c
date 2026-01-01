#!/bin/bash

PURPLE="./purple_c"
FAIL=0

run_test() {
    name="$1"
    input="$2"
    expected="$3"

    echo -n "Test: $name ... "
    output=$(echo "$input" | $PURPLE 2>&1)
    
    if echo "$output" | grep -Fq "$expected"; then
        echo "PASS"
    else
        echo "FAIL"
        echo "Input:"
        echo "$input"
        echo "Expected to find:"
        echo "$expected"
        echo "Actual output:"
        echo "$output"
        FAIL=1
    fi
}

echo "Running Purple C Scratch Tests..."

# 1. Interpretation
run_test "Interpretation" \
    "(+ 1 2)" \
    "Result: 3"

# 2. Lifting (Compilation)
run_test "Lifting" \
    "(lift 42)" \
    "mk_int(42);"

# 3. Collapsing (Mixed Static/Dynamic)
run_test "Collapsing" \
    "(+ 10 (lift 20))" \
    "add(mk_int(10), mk_int(20));"

# 4. Let Binding & CLEAN Strategy
# With Phase 2 Shape Analysis, we now use shape-based free:
# (let ((x (lift 10))) (+ x x))
# Generates:
# Obj* x = mk_int(10);
# Obj* res = add(x, x);
# free_tree(x); // ASAP Clean (shape: TREE)
run_test "ASAP CLEAN" \
    "(let ((x (lift 10))) (+ x x))" \
    "// ASAP Clean (shape:"

# 5. ASAP SCAN Strategy
# (scan 'List (lift 1))
run_test "ASAP SCAN" \
    "(scan 'List (lift 1))" \
    "scan_List(mk_int(1)); // ASAP Mark"

# 5b. ASAP SCAN with non-symbol type argument (should not crash)
# Before fix: would crash with NULL dereference
# After fix: returns NIL gracefully
run_test "ASAP-ScanNonSymbol" \
    "(+ (scan 42 (lift 1)) 0)" \
    "Result:"

# 6. Recursive Structure (List Scanner Generation)
# The compiler prints the generated scanner at startup
run_test "Scanner Generation" \
    "(lift 0)" \
    "void scan_List(Obj* x) {"

# =============================================================================
# NEW OPTIMIZATION TESTS (Phases 2-8)
# =============================================================================

# 7. Phase 5: Dynamic Free List
# Check that the dynamic free list structure is generated
run_test "Phase5-DynamicFreeList" \
    "(lift 0)" \
    "typedef struct FreeNode"

# 8. Phase 3: Stack Allocation Pool
# Check that stack allocation infrastructure is generated
run_test "Phase3-StackPool" \
    "(lift 0)" \
    "STACK_POOL_SIZE"

# 9. ASAP Scanner (for traversal/debugging, NOT garbage collection)
# ASAP uses compile-time deallocation, not runtime GC
run_test "ASAP-Scanner" \
    "(lift 0)" \
    "// Note: ASAP uses compile-time free injection, not runtime GC"

run_test "ASAP-ClearMarks" \
    "(lift 0)" \
    "void clear_marks_List(Obj* x)"

# 10. Phase 7: Multi-binding let (interpretation)
run_test "Phase7-MultiLet-Interp" \
    "(let ((x 10) (y 20)) (+ x y))" \
    "Result: 30"

# 11. Phase 7: Multi-binding let (compilation)
run_test "Phase7-MultiLet-Compile" \
    "(let ((x (lift 10)) (y (lift 20))) (+ x y))" \
    "Obj* x = mk_int(10)"

# 12. Phase 3: Escape analysis - values passed to functions are marked as escaping
# When x is used in (+ x x), it escapes via argument, so no stack-candidate annotation
run_test "Phase3-EscapeArg" \
    "(let ((x (lift 10))) (+ x x))" \
    "Obj* x = mk_int(10);"

# 13. Phase 4: Capture tracking - lambda captures should not be freed
run_test "Phase4-CaptureNoFree" \
    "(let ((x (lift 10))) (lambda (y) (+ x y)))" \
    "captured by closure"

# =============================================================================
# ADDITIONAL OPTIMIZATION TESTS (Phases 2-5)
# =============================================================================

# 14. Phase 2: Shape Analysis - shape-based free strategy
run_test "Phase2-ShapeAnalysis" \
    "(lift 0)" \
    "Phase 2: Shape-based deallocation"

# 15. Phase 2: Tree shape free function
run_test "Phase2-FreeTree" \
    "(lift 0)" \
    "void free_tree(Obj* x)"

# 16. Phase 2: DAG shape refcount function
run_test "Phase2-DecRef" \
    "(lift 0)" \
    "void dec_ref(Obj* x)"

# 17. Phase 3: Weak reference support
run_test "Phase3-WeakRef" \
    "(lift 0)" \
    "typedef struct WeakRef"

# 18. Phase 3: Weak reference deref function
run_test "Phase3-DerefWeak" \
    "(lift 0)" \
    "void* deref_weak(WeakRef* w)"

# 19. Phase 4: Perceus reuse runtime
run_test "Phase4-PerceusReuse" \
    "(lift 0)" \
    "Phase 4: Perceus Reuse Analysis Runtime"

# 20. Phase 4: Try reuse function
run_test "Phase4-TryReuse" \
    "(lift 0)" \
    "Obj* try_reuse(Obj* old, size_t size)"

# 21. Phase 4: Reuse as int function
run_test "Phase4-ReuseAsInt" \
    "(lift 0)" \
    "Obj* reuse_as_int(Obj* old, long value)"

# =============================================================================
# PHASE 6b: ISMM 2024 - SCC-based Reference Counting
# =============================================================================

# 22. Phase 6b: SCC structure generation
run_test "Phase6b-SCCStruct" \
    "(lift 0)" \
    "typedef struct SCC"

# 23. Phase 6b: Tarjan's SCC algorithm
run_test "Phase6b-Tarjan" \
    "(lift 0)" \
    "void tarjan_strongconnect"

# 24. Phase 6b: Freeze function
run_test "Phase6b-FreezeCyclic" \
    "(lift 0)" \
    "SCC* freeze_cyclic(Obj* root)"

# 25. Phase 6b: SCC release function
run_test "Phase6b-ReleaseSCC" \
    "(lift 0)" \
    "void release_scc(SCC* scc)"

# =============================================================================
# PHASE 7: Deferred RC Fallback
# =============================================================================

# 26. Phase 7: Deferred decrement structure
run_test "Phase7-DeferredDec" \
    "(lift 0)" \
    "typedef struct DeferredDec"

# 27. Phase 7: Safe point function
run_test "Phase7-SafePoint" \
    "(lift 0)" \
    "void safe_point()"

# 28. Phase 7: Flush all deferred
run_test "Phase7-FlushDeferred" \
    "(lift 0)" \
    "void flush_all_deferred()"

# 29. Phase 7: Deferred batch processing
run_test "Phase7-ProcessBatch" \
    "(lift 0)" \
    "process_deferred_batch"

# =============================================================================
# ADDITIONAL INTEGRATION TESTS
# =============================================================================

# 30. Primary strategy comment
run_test "PrimaryStrategy" \
    "(lift 0)" \
    "Primary Strategy: ASAP + ISMM 2024"

# 31. Type tag support (is_pair)
run_test "TypeTag-IsPair" \
    "(lift 0)" \
    "int is_pair"

# =============================================================================
# PHASE 8: Arena Allocation
# =============================================================================

# 32. Phase 8: Arena struct
run_test "Phase8-ArenaStruct" \
    "(lift 0)" \
    "typedef struct Arena"

# 33. Phase 8: Arena create function
run_test "Phase8-ArenaCreate" \
    "(lift 0)" \
    "Arena* arena_create"

# 34. Phase 8: Arena alloc function
run_test "Phase8-ArenaAlloc" \
    "(lift 0)" \
    "void* arena_alloc"

# 35. Phase 8: Arena destroy function
run_test "Phase8-ArenaDestroy" \
    "(lift 0)" \
    "void arena_destroy"

# 36. Phase 8: Arena-aware int allocator
run_test "Phase8-ArenaMkInt" \
    "(lift 0)" \
    "Obj* arena_mk_int"

# 37. Phase 8: Arena-aware pair allocator
run_test "Phase8-ArenaMkPair" \
    "(lift 0)" \
    "Obj* arena_mk_pair"

# =============================================================================
# PHASE 9: Destination-Passing Style (DPS)
# =============================================================================

# 38. Phase 9: DPS Dest struct
run_test "Phase9-DestStruct" \
    "(lift 0)" \
    "typedef struct Dest"

# 39. Phase 9: Stack destination macro
run_test "Phase9-StackDest" \
    "(lift 0)" \
    "#define STACK_DEST"

# 40. Phase 9: DPS write_int function
run_test "Phase9-WriteInt" \
    "(lift 0)" \
    "Obj* write_int(Dest* dest, long value)"

# 41. Phase 9: DPS write_pair function
run_test "Phase9-WritePair" \
    "(lift 0)" \
    "Obj* write_pair(Dest* dest, Obj* a, Obj* b)"

# 42. Phase 9: DPS add function
run_test "Phase9-AddDPS" \
    "(lift 0)" \
    "Obj* add_dps(Dest* dest"

# 43. Phase 9: DPS map function
run_test "Phase9-MapDPS" \
    "(lift 0)" \
    "void map_dps"

# 44. Phase 9: DPS fold function
run_test "Phase9-FoldDPS" \
    "(lift 0)" \
    "Obj* fold_dps"

# =============================================================================
# PHASE 10: Exception Handling
# =============================================================================

# 45. Phase 10: Exception type enum
run_test "Phase10-ExceptionType" \
    "(lift 0)" \
    "typedef enum"

# 46. Phase 10: Exception struct
run_test "Phase10-ExceptionStruct" \
    "(lift 0)" \
    "typedef struct Exception"

# 47. Phase 10: Exception frame struct
run_test "Phase10-ExcFrame" \
    "(lift 0)" \
    "typedef struct ExcFrame"

# 48. Phase 10: Exception push function
run_test "Phase10-ExcPush" \
    "(lift 0)" \
    "ExcFrame* exc_push()"

# 49. Phase 10: Exception pop function
run_test "Phase10-ExcPop" \
    "(lift 0)" \
    "void exc_pop()"

# 50. Phase 10: Register cleanup function
run_test "Phase10-RegisterCleanup" \
    "(lift 0)" \
    "void exc_register_cleanup"

# 51. Phase 10: Run cleanups (landing pad)
run_test "Phase10-RunCleanups" \
    "(lift 0)" \
    "void exc_run_cleanups()"

# 52. Phase 10: Throw function
run_test "Phase10-ExcThrow" \
    "(lift 0)" \
    "void exc_throw"

# 53. Phase 10: TRY macro
run_test "Phase10-TryMacro" \
    "(lift 0)" \
    "#define TRY"

# 54. Phase 10: CATCH macro
run_test "Phase10-CatchMacro" \
    "(lift 0)" \
    "#define CATCH"

# =============================================================================
# PHASE 11: Concurrency Support
# =============================================================================

# 55. Phase 11: Thread-local storage
run_test "Phase11-ThreadLocal" \
    "(lift 0)" \
    "__thread int THREAD_ID"

# 56. Phase 11: Concurrent object struct
run_test "Phase11-ConcObj" \
    "(lift 0)" \
    "typedef struct ConcObj"

# 57. Phase 11: Atomic increment
run_test "Phase11-ConcIncRef" \
    "(lift 0)" \
    "void conc_inc_ref"

# 58. Phase 11: Atomic decrement
run_test "Phase11-ConcDecRef" \
    "(lift 0)" \
    "void conc_dec_ref"

# 59. Phase 11: Message channel struct
run_test "Phase11-MsgChannel" \
    "(lift 0)" \
    "typedef struct MsgChannel"

# 60. Phase 11: Channel create
run_test "Phase11-ChannelCreate" \
    "(lift 0)" \
    "MsgChannel* channel_create"

# 61. Phase 11: Channel send (ownership transfer)
run_test "Phase11-ChannelSend" \
    "(lift 0)" \
    "int channel_send"

# 62. Phase 11: Channel receive (ownership transfer)
run_test "Phase11-ChannelRecv" \
    "(lift 0)" \
    "ConcObj* channel_recv"

# 63. Phase 11: Spawn thread helper
run_test "Phase11-SpawnThread" \
    "(lift 0)" \
    "pthread_t spawn_thread"

# 64. Phase 11: Freeze for immutable sharing
run_test "Phase11-ConcFreeze" \
    "(lift 0)" \
    "void conc_freeze"

# =============================================================================
# LANGUAGE PRIMITIVES
# =============================================================================

# 65. Multiplication
run_test "Prim-Mul" \
    "(* 6 7)" \
    "Result: 42"

# 66. Division
run_test "Prim-Div" \
    "(/ 42 6)" \
    "Result: 7"

# 67. Modulo
run_test "Prim-Mod" \
    "(% 17 5)" \
    "Result: 2"

# 68. Equality
run_test "Prim-Eq" \
    "(= 5 5)" \
    "Result: t"

# 69. Less than
run_test "Prim-Lt" \
    "(< 3 5)" \
    "Result: t"

# 70. Greater than
run_test "Prim-Gt" \
    "(> 5 3)" \
    "Result: t"

# 71. Less or equal
run_test "Prim-Le" \
    "(<= 5 5)" \
    "Result: t"

# 72. Greater or equal
run_test "Prim-Ge" \
    "(>= 5 5)" \
    "Result: t"

# 73. Logical and
run_test "Prim-And" \
    "(and t t)" \
    "Result: t"

# 74. Logical or
run_test "Prim-Or" \
    "(or nil t)" \
    "Result: t"

# 75. Logical not
run_test "Prim-Not" \
    "(not nil)" \
    "Result: t"

# 76. car
run_test "Prim-Car" \
    "(car (cons 1 2))" \
    "Result: 1"

# 77. cdr
run_test "Prim-Cdr" \
    "(cdr (cons 1 2))" \
    "Result: 2"

# 78. fst (alias for car)
run_test "Prim-Fst" \
    "(fst (cons 3 4))" \
    "Result: 3"

# 79. snd (alias for cdr)
run_test "Prim-Snd" \
    "(snd (cons 3 4))" \
    "Result: 4"

# 80. null?
run_test "Prim-Null" \
    "(null? nil)" \
    "Result: t"

# 81. letrec (factorial)
run_test "Prim-Letrec" \
    "(letrec ((fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1))))))) (fact 5))" \
    "Result: 120"

# 82. Short-circuit and (false case)
run_test "Prim-AndShort" \
    "(and nil t)" \
    "Result: ()"

# 83. Short-circuit or (first true)
run_test "Prim-OrShort" \
    "(or t nil)" \
    "Result: t"

# 84. Overflow protection in generated add function
run_test "Prim-AddOverflowProtect" \
    "(lift 0)" \
    "if ((b->i > 0 && a->i > LONG_MAX - b->i)"

# 85. Overflow protection in generated sub function
run_test "Prim-SubOverflowProtect" \
    "(lift 0)" \
    "if ((b->i < 0 && a->i > LONG_MAX + b->i)"

# 86. Overflow protection in generated mul function
run_test "Prim-MulOverflowProtect" \
    "(lift 0)" \
    "if (a->i > 0 && b->i > 0 && a->i > LONG_MAX / b->i)"

# 87. Channel create rejects zero capacity (prevents division by zero)
run_test "Phase11-ChannelZeroCap" \
    "(lift 0)" \
    "if (capacity <= 0) return NULL"

if [ $FAIL -eq 0 ]; then
    echo "All tests passed!"
    exit 0
else
    echo "Some tests failed."
    exit 1
fi
