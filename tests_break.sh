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

unit_test_expect_success() {
  name="$1"
  src="$2"

  echo -n "Break Test (unit): $name ... "
  gcc -std=c11 -Wall -Wextra -Isrc "$src" src/memory/scc.c src/types.c -o "$tmpdir/unit" 2>"$tmpdir/err"
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

# 3) Perceus reuse should re-tag objects when reusing memory
output_expect_contains "PerceusReuseResetsIsPair" \
  "(lift 0)" \
  "obj->is_pair = 0"

output_expect_contains "PerceusReuseResetsSccId" \
  "(lift 0)" \
  "obj->scc_id = -1"

# 4) SCC runtime should not walk SCC list via pointer arithmetic
output_expect_absent "SccNoPointerWalk" \
  "(lift 0)" \
  "s = (SCC*)((char*)s + sizeof(SCC))"

# 5) SCC registry should not lose nodes when pushing onto stack
unit_test_expect_success "SccNodeMapIntegrity" \
  "tests/unit_scc.c"

exit $FAIL
