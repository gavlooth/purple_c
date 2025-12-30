#!/bin/bash

# Compile the repro cases
echo "Compiling repro_issues.c..."
gcc -g -I. -Isrc -lpthread \
    tests/repro_issues.c \
    src/memory/arena.c \
    -o tests/repro_issues

echo "Compiling repro_overflow.c..."
gcc -g tests/repro_overflow.c -o tests/repro_overflow

echo "Compiling test_escape.c..."
gcc -g -I./src tests/test_escape.c \
    src/types.c \
    src/analysis/escape.c \
    src/analysis/shape.c \
    src/eval/eval.c \
    src/codegen/codegen.c \
    src/analysis/dps.c \
    src/util/dstring.c \
    src/util/hashmap.c \
    -o tests/test_escape

echo "---------------------------------------------------"
echo "Running Issue 1: Concurrency Race (Expect Use-After-Free)"
./tests/repro_issues concurrency
echo "Exit Code: $?"

echo "---------------------------------------------------"
echo "Running Issue 2: Arena Leak (Expect FAIL)"
./tests/repro_issues arena
echo "Exit Code: $?"

echo "---------------------------------------------------"
echo "Running Issue 3: Unsafe Shape Analysis (Expect Abort/Crash)"
./tests/repro_issues shape
echo "Exit Code: $?"

echo "---------------------------------------------------"
echo "Running Issue 4: Weak Reference Dangling (Expect FAIL)"
./tests/repro_issues weakref
echo "Exit Code: $?"

echo "---------------------------------------------------"
echo "Running Issue 5: Reuse Optimization Leak (Expect FAIL)"
./tests/repro_issues reuse
echo "Exit Code: $?"

echo "---------------------------------------------------"
echo "Running Issue 6: Escape Analysis Unsoundness (Expect FAIL)"
./tests/test_escape
echo "Exit Code: $?"

echo "---------------------------------------------------"
echo "Running Buffer Overflow Test (Expect Stack Smashing)"
./tests/repro_overflow
echo "Exit Code: $?"
