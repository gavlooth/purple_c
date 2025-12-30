# Purple C (Scratch Implementation)

A standalone C implementation of the **Purple** language strategy ("Collapsing Towers of Interpreters"), augmented with **ASAP** (As Static As Possible) memory management.

## Overview

This project demonstrates two advanced compiler techniques working in harmony:

1.  **Stage Polymorphism (Purple Strategy)**:
    The core evaluator is stage-polymorphic. It functions simultaneously as:
    *   **Interpreter**: When evaluating concrete values (Integers, Lists).
    *   **Compiler**: When evaluating symbolic `Code` values.
    This allows the same codebase to run programs immediately or compiling them to C, simply by "lifting" inputs.

2.  **ASAP Memory Management**:
    The compiled output uses a static memory management regime instead of a Garbage Collector:
    *   **SCAN Generation**: Type-specific scanner functions (`scan_List`, etc.) are generated at compile-time (based on "Paths").
    *   **CLEAN Injection**: The compiler performs a linear-scan approximation to detect dead variables and injects `free_obj()` calls automatically at the end of their scope.
    *   **Deferred Freeing**: Runtime optimization to prevent double-frees during complex graph traversals.

## Features

*   **Reflective Tower**: `MEnv` (Meta-Environment) allows modifying the evaluator at runtime (e.g., changing how `app` or `let` works).
*   **EM (Execute-at-Metalevel)**: Jump up the tower to inspect/modify the evaluator.
*   **Lisp-like Syntax**: S-expressions with `lambda`, `let`, `if`, `quote`, etc.
*   **C Code Generation**: Compiles to standalone C code.

## Usage

### Build
```bash
make
```

### Run
Interactive mode:
```bash
./purple_c
```

Run an example:
```bash
cat examples/demo.purple | ./purple_c
```

### Testing
```bash
make test
```

## Structure

*   `main.c`: Single-file implementation (Lexer, Parser, AST, Evaluator, Compiler, Runtime).
*   `examples/`: Sample Purple programs.
*   `tests.sh`: Regression test suite.

## References

*   *Collapsing Towers of Interpreters* (Amin & Rompf, POPL 2018)
*   *ASAP: As Static As Possible memory management* (Proust, 2017)
*   *Practical Static Memory Management* (Corbyn, 2020)
