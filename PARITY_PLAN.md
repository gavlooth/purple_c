# Purple C - Parity Plan with Go Implementation

## Overview

This plan brings the C implementation to feature parity with the Go version (`purple_go`).

**Both versions have the same memory infrastructure:**
- ✅ ASAP (compile-time free insertion)
- ✅ SCC-based RC (Tarjan's algorithm for frozen cycles)
- ✅ Deferred RC (bounded O(k) for mutable cycles)
- ✅ Arena allocation
- ✅ Symmetric RC
- ✅ GenRef/Region/Constraint refs
- ✅ Shape analysis (TREE/DAG/CYCLIC)
- ✅ Escape analysis
- ✅ RC optimization (~95% ops eliminated)
- ✅ TypeRegistry with back-edge detection

**C is missing LANGUAGE FEATURES that Go now has:**
- ✅ Mutable state (`box`, `set!`, `define`) - IMPLEMENTED
- ✅ I/O primitives (`display`, `newline`, `read`, `print`) - IMPLEMENTED
- ❌ Full continuations (`call/cc`, `prompt`/`control`)
- ❌ CSP concurrency (`go`, `make-chan`, `chan-send!`, `chan-recv!`, `select`) - Types defined, blocking ops TBD
- ❌ deftype language form (infrastructure exists, not wired to eval)

**Goal:** Add language features to C, then wire them to existing memory infrastructure.

---

## Dependency Graph

```
Part A: Language Features (from Go)
──────────────────────────────────
Phase A1 (Mutable State)
    │
    ▼
Phase A2 (I/O Primitives)
    │
    ▼
Phase A3 (Full Continuations) ───► Phase A4 (CSP Concurrency)
    │
    ▼
Phase A5 (deftype Language Form)

Part B: Memory Integration (leverage existing C infrastructure)
──────────────────────────────────────────────────────────────
Phase B1 (deftype → TypeRegistry wiring)
    │
    ▼
Phase B2 (Back-edge heuristics for deftype)
    │
    ▼
Phase B3 (Weak field codegen)
    │
    ▼
Phase B4 (Exception landing pads) ◄─── Phase B5 (Interprocedural analysis)
    │
    ▼
Phase B6 (Concurrency ownership) ───► Phase B7 (Shape + Perceus integration)
    │
    ▼
Phase B8 (Minor primitives)
```

---

# Part A: Language Feature Parity

## Phase A1: Mutable State (Critical)

**Problem**: The C version lacks mutable cells, `set!`, and `define`.

### Current State
- `src/types.h` has basic types but no `T_CELL` (mutable box)
- `src/eval/eval.c` has no `set!` or `define` handlers
- Environment is immutable (extend only)

### Tasks

| Task | File | Description |
|------|------|-------------|
| A1.1 | `src/types.h` | Add `T_CELL` type for mutable boxes |
| A1.2 | `src/types.c` | Add `mk_cell()`, cell accessors |
| A1.3 | `src/eval/eval.c` | Add `box`, `unbox`, `set-box!` primitives |
| A1.4 | `src/eval/eval.c` | Add `set!` special form (mutate existing binding) |
| A1.5 | `src/eval/eval.c` | Add `define` special form (global definitions) |
| A1.6 | `src/eval/eval.c` | Add global environment with mutex protection |
| A1.7 | `src/eval/eval.c` | Modify `env_set()` to mutate in place |

### Implementation Details

```c
// src/types.h additions
#define T_CELL 15  // Mutable box

typedef struct Value {
    // ... existing fields
    struct Value* cell_value;  // For T_CELL
} Value;

Value* mk_cell(Value* initial);
Value* cell_get(Value* cell);
void cell_set(Value* cell, Value* val);
```

```c
// src/eval/eval.c - set! implementation
case SYM_SET_BANG:
    // (set! var val)
    Value* var_sym = car(args);
    Value* new_val = eval(cadr(args), menv);
    // Try local env first, then global
    if (!env_set(menv->env, var_sym, new_val)) {
        if (!global_env_set(var_sym, new_val)) {
            return mk_error("set!: unbound variable");
        }
    }
    return new_val;
```

### Acceptance Criteria
- [ ] `(define x 0)` creates global binding
- [ ] `(set! x 1)` mutates global binding
- [ ] `(let ((y (box 10))) (set-box! y 20) (unbox y))` → 20
- [ ] Meta-level modifications persist via `(EM (set! base-eval ...))`

---

## Phase A2: I/O Primitives

**Problem**: No `display`, `newline`, `read` primitives.

### Tasks

| Task | File | Description |
|------|------|-------------|
| A2.1 | `src/eval/eval.c` | Add `display` primitive (print value) |
| A2.2 | `src/eval/eval.c` | Add `newline` primitive |
| A2.3 | `src/eval/eval.c` | Add `read` primitive (parse S-expr from stdin) |
| A2.4 | `src/eval/eval.c` | Add `read-char` primitive |
| A2.5 | `src/main.c` | Improve REPL with proper I/O |

### Implementation

```c
// src/eval/eval.c
Value* prim_display(Value* args, Value* menv) {
    Value* v = car(args);
    print_value(v, stdout);
    return nil;
}

Value* prim_newline(Value* args, Value* menv) {
    printf("\n");
    fflush(stdout);
    return nil;
}

Value* prim_read(Value* args, Value* menv) {
    char buffer[4096];
    if (fgets(buffer, sizeof(buffer), stdin)) {
        return parse(buffer);
    }
    return mk_error("read: EOF");
}
```

### Acceptance Criteria
- [ ] `(display "hello")` prints "hello"
- [ ] `(newline)` prints newline
- [ ] `(read)` reads and parses S-expression from stdin

---

## Phase A3: Full Continuations

**Problem**: `call/cc` is partially mocked; need real continuation capture.

### Current State
- `src/types.h` has `T_CONT` but it's a stub
- No actual continuation capture mechanism

### Tasks

| Task | File | Description |
|------|------|-------------|
| A3.1 | `src/types.h` | Enhance `T_CONT` with full continuation data |
| A3.2 | `src/types.c` | Add `mk_cont()` with environment + handler capture |
| A3.3 | `src/eval/eval.c` | Implement `call/cc` using setjmp/longjmp |
| A3.4 | `src/eval/eval.c` | Add `prompt` (delimiter) special form |
| A3.5 | `src/eval/eval.c` | Add `control` (capture) special form |
| A3.6 | `src/eval/eval.c` | Add continuation invocation in apply |
| A3.7 | Tests | Add continuation test cases |

### Implementation Strategy

Use setjmp/longjmp for escape continuations:

```c
// src/types.h
typedef struct Continuation {
    jmp_buf escape_point;
    Value* result;
    int tag;  // Unique tag for matching
} Continuation;

#define T_CONT 12

typedef struct Value {
    // ...
    Continuation* cont;
} Value;
```

```c
// src/eval/eval.c
static int cont_tag_counter = 0;
static jmp_buf* current_escape = NULL;

Value* eval_call_cc(Value* args, Value* menv) {
    Value* proc = eval(car(args), menv);

    int tag = ++cont_tag_counter;
    jmp_buf escape;
    Value* result = NULL;

    if (setjmp(escape) == 0) {
        // Normal path: create continuation and call proc
        Value* cont = mk_cont(&escape, tag);
        result = apply(proc, cons(cont, nil), menv);
    } else {
        // Escape path: continuation was invoked
        result = current_escape_result;
    }

    return result;
}

Value* invoke_continuation(Value* cont, Value* val) {
    current_escape_result = val;
    longjmp(*cont->cont->escape_point, 1);
    return NULL;  // Never reached
}
```

### Acceptance Criteria
- [ ] `(call/cc (lambda (k) (+ 1 (k 42))))` → 42
- [ ] `(prompt (+ 1 (control k 42)))` → 42
- [ ] `(prompt (+ 1 (control k (k 10))))` → 11
- [ ] Nested continuations work correctly

---

## Phase A4: CSP Concurrency

**Problem**: Memory framework exists but no language primitives for channels/goroutines.

### Current State
- `src/memory/concurrent.h` has ownership transfer framework
- No `T_CHAN`, `T_PROCESS` types
- No `go`, `chan`, `select` primitives

### Tasks

| Task | File | Description |
|------|------|-------------|
| A4.1 | `src/types.h` | Add `T_CHAN` (channel) type |
| A4.2 | `src/types.h` | Add `T_PROCESS` (green thread) type |
| A4.3 | `src/types.c` | Add `mk_chan()`, channel operations |
| A4.4 | `src/eval/scheduler.h` (new) | Define scheduler for green threads |
| A4.5 | `src/eval/scheduler.c` (new) | Implement run queue, parking, unparking |
| A4.6 | `src/eval/eval.c` | Add `make-chan` primitive |
| A4.7 | `src/eval/eval.c` | Add `chan-send!`, `chan-recv!` primitives |
| A4.8 | `src/eval/eval.c` | Add `go` special form |
| A4.9 | `src/eval/eval.c` | Add `select` special form |
| A4.10 | `src/memory/concurrent.c` | Wire ownership transfer to channel ops |

### Implementation

```c
// src/types.h
#define T_CHAN 16
#define T_PROCESS 17

typedef struct Channel {
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
    Value** buffer;
    int capacity;
    int size;
    int head;
    int tail;
    int closed;
} Channel;

typedef struct Process {
    Value* thunk;      // Function to execute
    Value* menv;       // Environment
    int state;         // READY, RUNNING, PARKED, DONE
    Value* result;     // Result when done
    struct Process* next;
} Process;
```

```c
// src/eval/scheduler.h
typedef struct Scheduler {
    pthread_mutex_t mutex;
    Process* run_queue_head;
    Process* run_queue_tail;
    Process* current;
    int running;
} Scheduler;

void scheduler_spawn(Value* thunk, Value* menv);
void scheduler_run(void);
void scheduler_park(Process* proc);
void scheduler_unpark(Process* proc, Value* value);
```

### Acceptance Criteria
- [ ] `(define ch (make-chan 10))` creates buffered channel
- [ ] `(chan-send! ch 42)` sends value
- [ ] `(chan-recv! ch)` receives value
- [ ] `(go (lambda () (chan-send! ch 1)))` spawns process
- [ ] Producer-consumer pattern works without deadlock

---

## Phase A5: deftype Language Form

**Problem**: Type registry exists in codegen but no language-level `deftype`.

### Current State
- `src/codegen/codegen.c` has `TypeRegistry`, `register_type()`, back-edge detection
- No parser/eval support for `(deftype Name (field Type) ...)`

### Tasks

| Task | File | Description |
|------|------|-------------|
| A5.1 | `src/types.h` | Add `T_USERTYPE` for user-defined type instances |
| A5.2 | `src/eval/eval.c` | Add `deftype` special form |
| A5.3 | `src/eval/eval.c` | Generate constructor `mk-Name` dynamically |
| A5.4 | `src/eval/eval.c` | Generate accessors `Name-field` dynamically |
| A5.5 | `src/eval/eval.c` | Generate predicate `Name?` dynamically |
| A5.6 | `src/codegen/codegen.c` | Wire deftype to existing TypeRegistry |
| A5.7 | `src/codegen/codegen.c` | Trigger back-edge analysis after deftype |

### Implementation

```c
// src/eval/eval.c
Value* eval_deftype(Value* args, Value* menv) {
    Value* name = car(args);
    Value* fields = cdr(args);

    char* type_name = sym_str(name);

    // Register with TypeRegistry
    TypeInfo* info = register_type(type_name);

    // Parse fields: (field-name type) or (field-name type :weak)
    for (Value* f = fields; !is_nil(f); f = cdr(f)) {
        Value* field_def = car(f);
        char* field_name = sym_str(car(field_def));
        char* field_type = sym_str(cadr(field_def));
        int is_weak = has_weak_annotation(field_def);

        add_field(info, field_name, field_type, is_weak);
    }

    // Build ownership graph and detect back-edges
    build_ownership_graph();
    analyze_back_edges();

    // Create constructor primitive: mk-Name
    Value* constructor = mk_type_constructor(type_name, info);
    global_define(mk_sym(concat("mk-", type_name)), constructor);

    // Create accessors: Name-field
    for (int i = 0; i < info->field_count; i++) {
        Value* accessor = mk_field_accessor(type_name, info->fields[i].name, i);
        global_define(mk_sym(concat3(type_name, "-", info->fields[i].name)), accessor);
    }

    // Create predicate: Name?
    Value* predicate = mk_type_predicate(type_name);
    global_define(mk_sym(concat(type_name, "?")), predicate);

    return mk_sym(type_name);
}
```

### Acceptance Criteria
- [ ] `(deftype Node (value int) (next Node))` registers type
- [ ] `(mk-Node 42 nil)` creates instance
- [ ] `(Node-value n)` returns field value
- [ ] `(Node? n)` returns true for Node instances
- [ ] `(deftype DList (val int) (next DList) (prev DList :weak))` marks prev as weak

---

# Part B: Memory Integration (Leverage Existing Infrastructure)

The C version already has sophisticated memory infrastructure. These phases wire new features to it.

## Phase B1: deftype → TypeRegistry Wiring

**Problem**: deftype creates types but codegen doesn't generate C structs.

### Tasks

| Task | File | Description |
|------|------|-------------|
| B1.1 | `src/codegen/codegen.c` | Track defined types in order for emission |
| B1.2 | `src/codegen/codegen.c` | Generate C struct forward declarations |
| B1.3 | `src/codegen/codegen.c` | Generate C struct definitions with proper field types |
| B1.4 | `src/codegen/codegen.c` | Generate `mk_Type()` C constructors |
| B1.5 | `src/codegen/codegen.c` | Generate field accessors in C |
| B1.6 | `src/codegen/codegen.c` | Generate release functions respecting weak fields |

### Acceptance Criteria
- [ ] Generated C code compiles for mutually recursive types
- [ ] Weak fields are not inc_ref'd in constructors
- [ ] Release functions skip weak fields

---

## Phase B2: Back-Edge Heuristics Enhancement

**Problem**: Expand naming heuristics for better auto-detection.

### Current State
- `src/codegen/codegen.c` has `is_back_edge_name()` with basic patterns

### Tasks

| Task | File | Description |
|------|------|-------------|
| B2.1 | `src/codegen/codegen.c` | Add patterns: `predecessor`, `ancestor`, `enclosing`, `*_back` |
| B2.2 | `src/codegen/codegen.c` | Add confidence scoring for ambiguous cases |
| B2.3 | `src/codegen/codegen.c` | Cache DFS results for performance |
| B2.4 | `src/codegen/codegen.c` | Minimize weak edges when breaking cycles |

### Acceptance Criteria
- [ ] All common back-edge patterns detected
- [ ] Analysis handles 50+ types efficiently
- [ ] Cycle breaking minimizes weak edge count

---

## Phase B3: Weak Field Codegen Integration

**Problem**: Runtime has weak ref support but codegen path incomplete.

### Tasks

| Task | File | Description |
|------|------|-------------|
| B3.1 | `src/codegen/codegen.c` | Complete release function generation |
| B3.2 | `src/codegen/codegen.c` | Generate type-aware scanners |
| B3.3 | `src/codegen/codegen.c` | Handle set! for weak fields (no RC) |
| B3.4 | `src/main.c` | Emit weak ref runtime support |

### Acceptance Criteria
- [ ] Doubly-linked list works without leaks
- [ ] valgrind clean on cyclic teardown

---

## Phase B4: Exception Landing Pads (Already Exists - Wire Up)

**Problem**: `src/memory/exception.c` exists but may not integrate with new features.

### Current State
- Exception handling infrastructure exists
- May need updates for new types (Cell, Chan, Process)

### Tasks

| Task | File | Description |
|------|------|-------------|
| B4.1 | `src/memory/exception.c` | Add cleanup for T_CELL |
| B4.2 | `src/memory/exception.c` | Add cleanup for T_CHAN |
| B4.3 | `src/memory/exception.c` | Add cleanup for T_PROCESS |
| B4.4 | `src/codegen/codegen.c` | Generate landing pads for new types |

### Acceptance Criteria
- [ ] Exceptions clean up cells, channels, processes
- [ ] No leaks when exceptions cross stack frames

---

## Phase B5: Interprocedural Analysis

**Problem**: Analysis is intraprocedural only.

### Tasks

| Task | File | Description |
|------|------|-------------|
| B5.1 | `src/analysis/summary.h` (new) | Define function summary structure |
| B5.2 | `src/analysis/summary.c` (new) | Compute summaries for functions |
| B5.3 | `src/analysis/escape.c` | Use summaries at call sites |
| B5.4 | `src/analysis/summary.c` | Hardcode summaries for primitives |

### Function Summary Format
```c
typedef struct FuncSummary {
    char* name;
    int param_count;
    OwnershipClass* params;  // BORROWED, CONSUMED, UNKNOWN
    OwnershipClass returns;  // FRESH, BORROWED, CONDITIONAL
    int allocates;           // Does it allocate?
    int stores_args;         // Does it store arguments?
} FuncSummary;
```

### Acceptance Criteria
- [ ] Summaries computed for user functions
- [ ] Call sites use callee summaries
- [ ] Cross-function ownership tracked

---

## Phase B6: Concurrency Ownership Transfer (Enhance Existing)

**Problem**: Framework exists but needs integration with new CSP primitives.

### Current State
- `src/memory/concurrent.c` has ownership transfer framework
- Needs integration with channels from Phase A4

### Tasks

| Task | File | Description |
|------|------|-------------|
| B6.1 | `src/memory/concurrent.c` | Add channel-aware ownership classes |
| B6.2 | `src/memory/concurrent.c` | Implement `ownership_transfer()` for send |
| B6.3 | `src/memory/concurrent.c` | Mark received values as locally owned |
| B6.4 | `src/analysis/escape.c` | Detect shared variables in `go` closures |
| B6.5 | `src/codegen/codegen.c` | Generate atomic RC for shared values |

### Ownership Classes
```c
typedef enum {
    OWNER_LOCAL,        // Thread-local, pure ASAP
    OWNER_TRANSFERRING, // Being sent over channel
    OWNER_RECEIVED,     // Received from channel
    OWNER_SHARED,       // Needs atomic RC
} ConcurrentOwnership;
```

### Acceptance Criteria
- [ ] Channel send transfers ownership
- [ ] Receiver becomes owner
- [ ] Shared captures use atomic RC
- [ ] No data races in generated code

---

## Phase B7: Shape + Perceus Integration

**Problem**: Shape analysis and Perceus reuse exist but need tighter integration.

### Current State
- `src/analysis/shape.c` computes TREE/DAG/CYCLIC
- `src/analysis/rcopt.c` has Perceus-style reuse
- May not fully integrate with deftype

### Tasks

| Task | File | Description |
|------|------|-------------|
| B7.1 | `src/analysis/shape.c` | Integrate with TypeRegistry cycle status |
| B7.2 | `src/analysis/rcopt.c` | Extend reuse pairing for user types |
| B7.3 | `src/codegen/codegen.c` | Generate reuse calls for deftype instances |
| B7.4 | `src/analysis/dps.c` | Extend DPS for user-defined types |

### Acceptance Criteria
- [ ] User types get correct shape classification
- [ ] Reuse optimization applies to user types
- [ ] DPS works for functions returning user types

---

## Phase B8: Minor Primitives

### Tasks

| Task | File | Description |
|------|------|-------------|
| B8.1 | `src/eval/eval.c` | Add `ctr-tag` (constructor name) |
| B8.2 | `src/eval/eval.c` | Add `ctr-arg` (constructor arg by index) |
| B8.3 | `src/eval/eval.c` | Add `reify-env` (environment as list) |
| B8.4 | `src/eval/eval.c` | Add `continuation?` predicate |
| B8.5 | `src/eval/eval.c` | Add `cell?` predicate |
| B8.6 | `src/eval/eval.c` | Add `chan?` predicate |
| B8.7 | `src/eval/eval.c` | Add `process?` predicate |

### Acceptance Criteria
- [ ] `(ctr-tag (cons 1 2))` → `'pair`
- [ ] `(ctr-arg (cons 1 2) 0)` → `1`
- [ ] `(reify-env)` returns current bindings as alist
- [ ] All type predicates work

---

## New Files to Create

| File | Phase | Purpose |
|------|-------|---------|
| `src/eval/scheduler.h` | A4 | Scheduler interface |
| `src/eval/scheduler.c` | A4 | Green thread scheduler |
| `src/analysis/summary.h` | B5 | Function summary interface |
| `src/analysis/summary.c` | B5 | Interprocedural analysis |

## Files to Modify

| File | Phases | Changes |
|------|--------|---------|
| `src/types.h` | A1, A3, A4, A5 | New types: T_CELL, T_CONT, T_CHAN, T_PROCESS, T_USERTYPE |
| `src/types.c` | A1, A3, A4, A5 | Constructors for new types |
| `src/eval/eval.c` | All A phases, B8 | New special forms and primitives |
| `src/codegen/codegen.c` | B1-B3, B7 | Type generation, weak fields |
| `src/memory/concurrent.c` | B6 | Channel ownership |
| `src/memory/exception.c` | B4 | Cleanup for new types |
| `src/analysis/shape.c` | B7 | TypeRegistry integration |
| `src/analysis/escape.c` | B5, B6 | Use summaries, detect shared |
| `src/analysis/rcopt.c` | B7 | User type reuse |
| `src/main.c` | A2 | REPL improvements |

---

## Testing Strategy

### Unit Tests (extend existing)
- `tests/unit_cell.c` (new) - Mutable cells
- `tests/unit_continuation.c` (new) - Continuations
- `tests/unit_channel.c` (new) - Channels
- `tests/unit_scheduler.c` (new) - Green threads
- `tests/unit_deftype.c` (new) - User-defined types
- `tests/unit_summary.c` (new) - Function summaries

### Integration Tests
- `tests/test_mutable.c` (new) - set!, define, cells
- `tests/test_csp.c` (new) - Producer-consumer, select
- `tests/test_types.c` (new) - deftype with back-edges

### Regression Tests
- Extend `tests.sh` with new test cases
- Ensure all 53 existing bug fixes still pass

### Validation
- `valgrind --leak-check=full` on all tests
- Thread sanitizer for concurrency tests

---

## Summary

| Phase | Focus | Dependencies |
|-------|-------|--------------|
| **A1** | Mutable State | None |
| **A2** | I/O Primitives | None |
| **A3** | Continuations | A1 (for set!) |
| **A4** | CSP Concurrency | A3 (for parking) |
| **A5** | deftype Form | A1 (for define) |
| **B1** | TypeRegistry Codegen | A5 |
| **B2** | Back-edge Heuristics | B1 |
| **B3** | Weak Field Codegen | B2 |
| **B4** | Exception Cleanup | A1, A4 |
| **B5** | Interprocedural | A5 |
| **B6** | Concurrency Ownership | A4, B5 |
| **B7** | Shape + Perceus | B1-B3 |
| **B8** | Minor Primitives | A1-A5 |

**Critical Path A**: A1 → A3 → A4 (language features)
**Critical Path B**: A5 → B1 → B2 → B3 → B7 (memory integration)

---

## Effort Estimate

| Part | Phases | Complexity |
|------|--------|------------|
| Part A (Language) | A1-A5 | High - Core language changes |
| Part B (Memory) | B1-B8 | Medium - Mostly wiring existing infrastructure |

**Total New Code**: ~3,000-4,000 lines C
**Modified Code**: ~2,000 lines across existing files

---

## NOT Included (Deferred)

- Tensor/libtorch integration
- HVM4 interaction nets
- De Bruijn indices (using named variables like Go)
