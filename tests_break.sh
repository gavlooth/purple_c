#!/bin/bash

# Tests that are expected to FAIL on current code.
# These should pass after the corresponding fixes are implemented.

PURPLE="./purple_c"
FAIL=0

tmpdir="$(mktemp -d -t purple_break_XXXXXX)"
cleanup() {
  rm -rf "$tmpdir"
}
trap cleanup EXIT

compile_expect_success() {
  name="$1"
  input="$2"

  echo -n "Break Test (compile): $name ... "
  "$PURPLE" "$input" > "$tmpdir/out.c" 2>"$tmpdir/err"
  gcc -std=c11 -Wall -Wextra -o "$tmpdir/out" "$tmpdir/out.c" >>"$tmpdir/err" 2>&1
  if [ $? -eq 0 ]; then
    echo "PASS"
  else
    echo "FAIL"
    echo "Input:"
    echo "$input"
    echo "Compiler output:"
    cat "$tmpdir/err"
    FAIL=1
  fi
}

output_expect_contains() {
  name="$1"
  input="$2"
  needle="$3"

  echo -n "Break Test (output contains): $name ... "
  output=$("$PURPLE" "$input" 2>&1)
  if echo "$output" | grep -Fq "$needle"; then
    echo "PASS"
  else
    echo "FAIL"
    echo "Input:"
    echo "$input"
    echo "Expected to find:"
    echo "$needle"
    echo "Actual output:"
    echo "$output"
    FAIL=1
  fi
}

output_expect_absent() {
  name="$1"
  input="$2"
  needle="$3"

  echo -n "Break Test (output absent): $name ... "
  output=$("$PURPLE" "$input" 2>&1)
  if echo "$output" | grep -Fq "$needle"; then
    echo "FAIL"
    echo "Input:"
    echo "$input"
    echo "Did NOT expect to find:"
    echo "$needle"
    echo "Actual output:"
    echo "$output"
    FAIL=1
  else
    echo "PASS"
  fi
}

output_expect_min_count() {
  name="$1"
  input="$2"
  needle="$3"
  min="$4"

  echo -n "Break Test (output count >= $min): $name ... "
  output=$("$PURPLE" "$input" 2>&1)
  count=$(echo "$output" | grep -F -c "$needle")
  if [ "$count" -ge "$min" ]; then
    echo "PASS"
  else
    echo "FAIL"
    echo "Input:"
    echo "$input"
    echo "Expected at least $min occurrences of:"
    echo "$needle"
    echo "Actual count: $count"
    FAIL=1
  fi
}

function_body() {
  fn_name="$1"
  input="$2"
  output=$("$PURPLE" "$input" 2>&1)
  echo "$output" | awk -v fn="void " -v name="$fn_name" '
    $0 ~ fn name {
      inside=1; depth=0;
    }
    inside {
      print;
      depth += gsub(/\{/,"{");
      depth -= gsub(/\}/,"}");
      if (depth == 0) exit;
    }
  '
}

function_body_expect_contains() {
  name="$1"
  input="$2"
  fn="$3"
  needle="$4"

  echo -n "Break Test (fn contains): $name ... "
  body=$(function_body "$fn" "$input")
  if echo "$body" | grep -Fq "$needle"; then
    echo "PASS"
  else
    echo "FAIL"
    echo "Function: $fn"
    echo "Expected to find:"
    echo "$needle"
    echo "Body:"
    echo "$body"
    FAIL=1
  fi
}

function_body_expect_absent() {
  name="$1"
  input="$2"
  fn="$3"
  needle="$4"

  echo -n "Break Test (fn absent): $name ... "
  body=$(function_body "$fn" "$input")
  if echo "$body" | grep -Fq "$needle"; then
    echo "FAIL"
    echo "Function: $fn"
    echo "Did NOT expect to find:"
    echo "$needle"
    echo "Body:"
    echo "$body"
    FAIL=1
  else
    echo "PASS"
  fi
}

unit_test_expect_success() {
  name="$1"
  src="$2"
  shift 2
  extra_sources="$@"

  echo -n "Break Test (unit): $name ... "
  gcc -Wall -Wextra -g -I. -Isrc "$src" $extra_sources -o "$tmpdir/unit" 2>"$tmpdir/err"
  if [ $? -ne 0 ]; then
    echo "FAIL"
    echo "Build failed:"
    cat "$tmpdir/err"
    FAIL=1
    return
  fi
  "$tmpdir/unit" >>"$tmpdir/err" 2>&1
  if [ $? -eq 0 ]; then
    echo "PASS"
  else
    echo "FAIL"
    echo "Runtime output:"
    cat "$tmpdir/err"
    FAIL=1
  fi
}

echo "Running Purple C Break Tests..."

# 1) Invalid C codegen for if-expressions
compile_expect_success "IfCodegenC" \
  "(if (lift 1) (lift 2) (lift 3))"

# 2) Cyclic free function should exist (mismatch with shape_free_strategy)
output_expect_contains "DeferredReleaseExists" \
  "(lift 0)" \
  "void deferred_release"

# 3) Tarjan traversal should guard against non-pair nodes
function_body_expect_contains "TarjanGuardsIsPair" \
  "(lift 0)" \
  "tarjan_strongconnect" \
  "v->is_pair"

# 4) Deferred RC should guard against non-pair nodes
function_body_expect_contains "DeferredGuardsIsPair" \
  "(lift 0)" \
  "process_deferred_batch" \
  "if (d->obj->is_pair)"

# 5) DAG refcounting should include an increment function
output_expect_contains "IncRefExists" \
  "(lift 0)" \
  "void inc_ref("

# 6) Weak refs should be invalidated on release
output_expect_contains "WeakInvalidateOnRelease" \
  "(lift 0)" \
  "invalidate_weak_refs_for"

# 7) Free list should prevent double-enqueue/reuse hazards
function_body_expect_contains "FreeObjGuardsReuse" \
  "(lift 0)" \
  "free_obj" \
  "mark < 0"

# 8) Generated SCC runtime should link SCCs into result list
function_body_expect_contains "SccRuntimeLinksResult" \
  "(lift 0)" \
  "tarjan_strongconnect" \
  "scc->result_next = *result"

# 9) Return-escape should not be freed at end of let
output_expect_absent "EscapeReturnNoFree" \
  "(let ((x (lift 1))) x)" \
  "free_tree(x)"

# 10) Letrec cyclic shapes should be detected as CYCLIC
unit_test_expect_success "LetrecShapeCyclic" \
  "tests/unit_shape_letrec.c" \
  "src/analysis/shape.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 11) SCC recomputation should work after mutation
unit_test_expect_success "SccRecomputeAfterMutation" \
  "tests/unit_scc_recompute.c" \
  "src/memory/scc.c" \
  "src/util/hashmap.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 12) Immutable RC should not be a no-op (leaks for shared frozen)
output_expect_absent "ImmutableRcIsNoop" \
  "(lift 0)" \
  "if (obj->is_immutable) return;  // Handled by SCC"

# 13) conc_freeze should guard against re-freeze cycles
function_body_expect_contains "ConcFreezeHasGuard" \
  "(lift 0)" \
  "conc_freeze" \
  "if (obj->is_immutable) return"

# 14) scan_* should not reuse refcount field
function_body_expect_absent "ScanDoesNotTouchMark" \
  "(lift 0)" \
  "scan_List" \
  "mark = 1"

# 15) SCC runtime should avoid fixed-size arrays
output_expect_absent "SccRuntimeNoFixedArrays" \
  "(lift 0)" \
  "TARJAN_NODES[4096]"

# 16) SCC registry should not use fixed-size arrays
output_expect_absent "SccRegistryNoFixedArrays" \
  "(lift 0)" \
  "SCC_REGISTRY[1024]"

# 17) Large list stringification should not overflow
unit_test_expect_success "ListToStrLarge" \
  "tests/unit_list_to_str_large.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 18) Deeply nested parse should not stack overflow
unit_test_expect_success "ParseDeepNesting" \
  "tests/unit_parse_deep.c" \
  "src/parser/parser.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 19) Arena should release external heap references (leak regression)
unit_test_expect_success "ArenaReleasesExternal" \
  "tests/unit_arena_external.c" \
  "src/memory/arena.c"

# 20) Bug fix tests - malloc/strdup checks, dstring safety, hashmap safety
unit_test_expect_success "BugFixTests" \
  "tests/unit_bugfixes.c" \
  "src/types.c" \
  "src/util/dstring.c" \
  "src/util/hashmap.c"

# 21) HashMap unit tests - full coverage of hashmap.c
unit_test_expect_success "HashMapTests" \
  "tests/unit_hashmap.c" \
  "src/util/hashmap.c"

# 22) Arena unit tests - full coverage of arena.c
unit_test_expect_success "ArenaTests" \
  "tests/unit_arena.c" \
  "src/memory/arena.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 23) Deferred RC unit tests - full coverage of deferred.c
unit_test_expect_success "DeferredTests" \
  "tests/unit_deferred.c" \
  "src/memory/deferred.c" \
  "src/util/hashmap.c"

# 24) SCC unit tests - full coverage of scc.c
unit_test_expect_success "SCCTests" \
  "tests/unit_scc.c" \
  "src/memory/scc.c" \
  "src/util/hashmap.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 25) Shape analysis unit tests - full coverage of shape.c
unit_test_expect_success "ShapeTests" \
  "tests/unit_shape.c" \
  "src/analysis/shape.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 26) Escape analysis unit tests - full coverage of escape.c
unit_test_expect_success "EscapeTests" \
  "tests/unit_escape.c" \
  "src/analysis/escape.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 27) Shape analysis should use free_tree for tree-shaped data
output_expect_contains "ShapeTreeUseFreeTree" \
  "(let ((x (cons (lift 1) (lift 2)))) x)" \
  "free_tree"

# 28) Generated code should have TARJAN_HASH for O(1) lookups
output_expect_contains "TarjanHasHash" \
  "(lift 0)" \
  "TARJAN_HASH"

# 29) Generated code should have DEFERRED_HASH for O(1) lookups
output_expect_contains "DeferredHasHash" \
  "(lift 0)" \
  "DEFERRED_HASH"

# 30) Generated release_scc should call invalidate_weak_refs_for
function_body_expect_contains "ReleaseSccInvalidatesWeak" \
  "(lift 0)" \
  "release_scc" \
  "invalidate_weak_refs_for"

# 31) Generated SCC should have result_next field for result list
output_expect_contains "SccHasResultNext" \
  "(lift 0)" \
  "result_next"

# 32) scan_List should guard is_pair before recursing
function_body_expect_contains "ScanListGuardsIsPair" \
  "(lift 0)" \
  "scan_List" \
  "if (x->is_pair)"

# 33) channel send/recv should reload head/tail after wait (non-decl assignment)
output_expect_contains "ChannelReloadsTail" \
  "(lift 0)" \
  "    tail = atomic_load(&ch->tail);"

output_expect_contains "ChannelReloadsHead" \
  "(lift 0)" \
  "    head = atomic_load(&ch->head);"

# 35) Stack object checks should use uintptr_t helper
output_expect_contains "StackObjUsesUintptr" \
  "(lift 0)" \
  "uintptr_t"

output_expect_contains "StackObjHasHelper" \
  "(lift 0)" \
  "is_stack_obj"

# 36) Codegen literal emission unit tests
unit_test_expect_success "CodegenLiteralExpr" \
  "tests/unit_codegen_literals.c" \
  "src/codegen/codegen.c" \
  "src/types.c" \
  "src/util/dstring.c"

# 37) Field-aware scanner should skip weak fields
unit_test_expect_success "FieldAwareScannerSkipsWeak" \
  "tests/unit_codegen_scanner.c" \
  "src/codegen/codegen.c" \
  "src/types.c" \
  "src/util/dstring.c"

exit $FAIL
