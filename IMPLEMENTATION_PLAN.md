# Purple Go Implementation Plan

## Reference: Collapsing Towers of Interpreters (POPL 2018)

Based on comparison with the original Purple/Black specification from:
- [Collapsing Towers of Interpreters](https://www.cs.purdue.edu/homes/rompf/papers/amin-popl18.pdf) - Amin & Rompf
- [LMS-Black Repository](https://github.com/namin/lms-black) - Reference implementation
- [Black Language](http://pllab.is.ocha.ac.jp/~asai/Black/) - Asai's original

---

## Part 1: Feature Comparison

### Core Language Features

| Feature | Paper Spec | Purple Go | Status |
|---------|-----------|-----------|--------|
| Integers | `I(n)` | `TInt` | ✅ Complete |
| Booleans | `B(b)` | Integers (0/1) | ✅ Complete (Lisp-style) |
| Symbols | `S(s)` | `TSym` | ✅ Complete |
| Strings | `Str(s)` | Character lists | ✅ Complete |
| Null/Nil | `N` | `TNil` | ✅ Complete |
| Pairs/Cons | `P(a,b)` | `TCell` | ✅ Complete |
| Closures | `Clo(...)` | `TLambda` | ✅ Complete |
| Continuations | `Cont(k)` | ❌ Missing | 🔴 Missing |
| Cells (mutable) | `Cell(v)` | ❌ Missing | 🔴 Missing |
| Primitives | `Prim(f)` | `TPrim` | ✅ Complete |
| Code values | Staged `Code[R]` | `TCode` | ✅ Complete |

### Special Forms

| Form | Paper Spec | Purple Go | Status |
|------|-----------|-----------|--------|
| `quote` | ✅ | ✅ `evalQuote` | ✅ Complete |
| `lambda` | ✅ | ✅ `defaultHLam` | ✅ Complete |
| `clambda` | ✅ Compile lambda | ✅ `defaultHClam` | ✅ Complete |
| `let` | ✅ | ✅ `defaultHLet` | ✅ Complete |
| `letrec` | ✅ | ✅ `evalLetrec` | ✅ Complete |
| `if` | ✅ | ✅ `defaultHIf` | ✅ Complete |
| `begin`/`do` | ✅ Sequential | ✅ `evalDo` | ✅ Complete |
| `set!` | ✅ Mutation | ❌ Missing | 🔴 Missing |
| `define` | ✅ Global def | ❌ Missing | 🔴 Missing |
| `EM` | ✅ Meta-level | ✅ `defaultHEM` | ✅ Complete |
| `lift` | ✅ Quote to code | ✅ `defaultHLft` | ✅ Complete |
| `run` | ✅ Execute code | ✅ `defaultHRun` | ✅ Complete |
| `call/cc` | ✅ Continuations | ❌ Missing | 🔴 Missing |
| `shift`/`reset` | ✅ Delimited cont | ⚠️ Partial (`shift` exists) | 🟡 Partial |

### Meta-Level API (Tower of Interpreters)

| Feature | Paper Spec | Purple Go | Status |
|---------|-----------|-----------|--------|
| 9-handler table | ✅ | ✅ `DefaultHandlers[9]` | ✅ Complete |
| `base-eval` | ✅ | ✅ `Eval()` | ✅ Complete |
| `base-apply` | ✅ | ✅ `apply()` | ✅ Complete |
| `eval-application` | ✅ | ✅ `defaultHApp` | ✅ Complete |
| Handler get/set | ✅ | ✅ `get-meta`/`set-meta!` | ✅ Complete |
| `with-handlers` | ✅ Local handlers | ✅ `evalWithHandlers` | ✅ Complete |
| `default-handler` | ✅ Delegate | ✅ `evalDefaultHandler` | ✅ Complete |
| `meta-level` | ✅ Get level | ✅ `evalMetaLevel` | ✅ Complete |
| Infinite tower | ✅ Lazy creation | ✅ `EnsureParent` | ✅ Complete |
| Reify/Reflect | ✅ Turn program↔data | ⚠️ Implicit | 🟡 Partial |

### Primitives

| Category | Paper Spec | Purple Go | Status |
|----------|-----------|-----------|--------|
| Type checks | `null?`, `number?`, `pair?`, `symbol?`, `continuation?` | All except `continuation?` | 🟡 Partial |
| Arithmetic | `+`, `-`, `*`, `<` | `+`, `-`, `*`, `/`, `%`, `<`, `>`, `<=`, `>=`, `=` | ✅ Extended |
| List ops | `car`, `cdr`, `cons` | ✅ Plus `map`, `filter`, `fold` | ✅ Extended |
| Comparison | `eq?` | ✅ `eq?`, `sym-eq?` | ✅ Complete |
| I/O | `display`, `newline` | ❌ Missing | 🔴 Missing |
| Cells | `cell-new`, `cell-read`, `cell-set` | ❌ Missing | 🔴 Missing |

---

## Part 2: Missing Features (Priority Order)

### Priority 1: Mutable State (Critical for Black/Purple semantics)

The original Black/Purple relies heavily on mutable cells for modifying the tower.

**Missing:**
1. `Cell` type - mutable reference cells
2. `set!` - variable mutation
3. `define` - global definitions
4. `cell-new`, `cell-read`, `cell-set` primitives

**Impact:** Without mutation, meta-level modifications are ephemeral.

**Implementation:**
```go
// pkg/ast/value.go
const TCell Tag = 12  // Mutable cell

type Value struct {
    // ... existing fields
    CellValue *Value  // For TCell - mutable contents
}

func NewCell(v *Value) *Value {
    return &Value{Tag: TCell, CellValue: v}
}
```

```go
// pkg/eval/eval.go - add set! handling
case "set!":
    varName := ast.SymStr(ast.Car(args))
    newVal := Eval(ast.Cadr(args), menv)
    EnvSet(menv.Env, varName, newVal)
    return newVal
```

**Effort:** 1-2 days

---

### Priority 2: First-Class Continuations

The paper specifies `call/cc` and continuation values for control flow.

**Missing:**
1. `Cont` type - reified continuations
2. `call/cc` - capture current continuation
3. `continuation?` - type predicate
4. `shift`/`reset` - delimited continuations (partially present)

**Impact:** Can't implement advanced control operators at user level.

**Implementation Strategy:**
- CPS transform the evaluator
- Add `Cont` value type
- Implement `call/cc` as primitive

**Effort:** 3-4 days (significant refactor)

---

### Priority 3: I/O Primitives

**Missing:**
1. `display` - output value
2. `newline` - output newline
3. `read` - input S-expression
4. `load` - load file

**Implementation:**
```go
// pkg/eval/primitives.go
"display": func(args *ast.Value, menv *ast.Value) *ast.Value {
    v := ast.Car(args)
    fmt.Print(ast.ValueToString(v))
    return ast.Nil
},
"newline": func(args *ast.Value, menv *ast.Value) *ast.Value {
    fmt.Println()
    return ast.Nil
},
```

**Effort:** 0.5 days

---

### Priority 4: User-Defined Types (deftype)

Infrastructure exists but not wired up.

**Missing:**
1. Parser support for `(deftype Name (field Type) ...)`
2. Evaluator integration
3. Back-edge detection (infrastructure complete)

**Files to modify:**
- `pkg/parser/parser.go` - parse deftype
- `pkg/eval/eval.go` - evalDeftype handler
- `pkg/codegen/types.go` - already has TypeRegistry

**Effort:** 2-3 days

---

### Priority 5: Complete Reification/Reflection API

**Missing:**
1. `reify` - explicit program→data conversion
2. `reflect` - explicit data→program conversion
3. `ctr-tag` - extract constructor tag
4. `ctr-arg` - extract constructor argument

**Current:** Implicit through quote/eval.

**Effort:** 1 day

---

## Part 3: Implementation Strategy

### Phase 1: Mutable State Foundation (Week 1)

**Goal:** Enable persistent meta-level modifications.

| Task | Files | Effort |
|------|-------|--------|
| Add `TCell` type | `pkg/ast/value.go` | 2 hrs |
| Add cell primitives | `pkg/eval/primitives.go` | 2 hrs |
| Implement `set!` | `pkg/eval/eval.go` | 4 hrs |
| Implement `define` | `pkg/eval/eval.go` | 2 hrs |
| Add global environment | `pkg/eval/eval.go` | 2 hrs |
| Tests | `pkg/eval/eval_test.go` | 4 hrs |

**Deliverable:** Working mutation, Black-style meta modifications.

---

### Phase 2: I/O and Practicality (Week 1-2)

**Goal:** Make the language practically usable.

| Task | Files | Effort |
|------|-------|--------|
| `display`/`newline` | `pkg/eval/primitives.go` | 2 hrs |
| `read` (S-expr input) | `pkg/eval/primitives.go` | 4 hrs |
| `load` (file loading) | `pkg/eval/primitives.go` | 2 hrs |
| REPL improvements | `main.go` | 2 hrs |
| Tests | `pkg/eval/primitives_test.go` | 2 hrs |

**Deliverable:** Interactive Purple programs with I/O.

---

### Phase 3: User-Defined Types (Week 2)

**Goal:** Enable custom data structures with automatic weak-edge detection.

| Task | Files | Effort |
|------|-------|--------|
| Parse `deftype` | `pkg/parser/parser.go` | 4 hrs |
| `evalDeftype` handler | `pkg/eval/eval.go` | 4 hrs |
| Wire to TypeRegistry | `pkg/codegen/types.go` | 4 hrs |
| Generate constructors | `pkg/codegen/codegen.go` | 4 hrs |
| Auto weak-edge | Already done | - |
| Tests | `pkg/test/deftype_test.go` | 4 hrs |

**Deliverable:** `(deftype Node (value int) (next Node) (prev Node))` works.

---

### Phase 4: Continuations (Week 3)

**Goal:** Full control operator support.

| Task | Files | Effort |
|------|-------|--------|
| Add `TCont` type | `pkg/ast/value.go` | 2 hrs |
| CPS evaluator refactor | `pkg/eval/eval.go` | 16 hrs |
| `call/cc` primitive | `pkg/eval/primitives.go` | 4 hrs |
| `shift`/`reset` | `pkg/eval/eval.go` | 8 hrs |
| `continuation?` pred | `pkg/eval/primitives.go` | 1 hr |
| Tests | `pkg/eval/continuation_test.go` | 8 hrs |

**Deliverable:** `(call/cc (lambda (k) ...))` works.

---

### Phase 5: Concurrency - CSP Model (Week 5-6)

**Goal:** Add Communicating Sequential Processes for safe concurrency.

**IMPORTANT:** CSP is built on top of continuations (Phase 4). The `go` form
must be able to **park** (suspend) and **resume** processes, which requires
capturing "what to do next" - a continuation.

Based on [Hoare's CSP](https://www.cs.cmu.edu/~crary/819-f09/Hoare78.pdf),
[Clojure core.async](https://github.com/clojure/core.async), and
[Manifold](https://github.com/clj-commons/manifold).

#### Why CSP over Manifold?

| Approach | Complexity | Memory Safety | Fit |
|----------|-----------|---------------|-----|
| **CSP (channels)** | Simple | Ownership transfer at send | ✅ Best |
| Manifold (deferreds+streams) | Complex | Requires careful coordination | ⚠️ |
| Actors (Erlang-style) | Medium | Copy semantics | 🟡 |
| Shared memory + locks | Simple API, hard correctness | Manual | ❌ |

CSP fits Purple's memory model: **ownership transfers through channels**.

#### Dependency: Continuations Required

```
┌─────────────────────────────────────────────────────────────┐
│                    CSP REQUIRES CONTINUATIONS                │
│                                                              │
│  (go                                                         │
│    (let ((x (<! ch1)))    ;; PARK here - save continuation  │
│      (>! ch2 (+ x 1))))   ;; PARK here - save continuation  │
│                                                              │
│  When (<! ch1) blocks:                                       │
│    1. Capture continuation k = (lambda (x) (>! ch2 (+ x 1))) │
│    2. Register k with channel's wait queue                   │
│    3. Yield to scheduler                                     │
│    4. When value arrives, scheduler invokes k                │
└─────────────────────────────────────────────────────────────┘
```

This is why Phase 4 (Continuations) must come first.

#### Core Primitives

```scheme
;; Channels
(chan)              ;; Create unbuffered channel
(chan 10)           ;; Create buffered channel (size 10)
(close! ch)         ;; Close channel

;; Communication (park current process if needed)
(>! ch val)         ;; Send - parks if full/unbuffered
(<! ch)             ;; Receive - parks if empty

;; Lightweight processes (green threads via continuations)
(go expr)           ;; Spawn process, returns channel for result
(go-loop bindings body)  ;; Loop inside process

;; Selection (wait on multiple channels)
(select
  ((<! ch1) => val (handle val))
  ((>! ch2 x) (after-send))
  (:default (no-match)))

;; Timeout
(timeout 1000)      ;; Channel that closes after N ms
```

#### Scheduler Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                     GREEN THREAD SCHEDULER                   │
│                                                              │
│  Run Queue: [Process1, Process2, Process3, ...]             │
│                                                              │
│  Each Process = (continuation, state)                        │
│                                                              │
│  Scheduler Loop:                                             │
│    1. Pop process from run queue                             │
│    2. Invoke its continuation                                │
│    3. If it parks (channel op): add to channel wait queue   │
│    4. If it yields: push back to run queue                  │
│    5. If it finishes: send result to its result channel     │
│    6. Repeat                                                 │
└─────────────────────────────────────────────────────────────┘
```

#### Memory Model Integration

```
┌─────────────────────────────────────────────────────────────┐
│                    OWNERSHIP TRANSFER                        │
│                                                              │
│  Process A              Channel              Process B       │
│  ─────────              ───────              ─────────       │
│  (let ((x (alloc)))     ┌─────┐                              │
│    (>! ch x)       ───▶ │  x  │ ───▶  (let ((y (<! ch)))    │
│    ;; x is DEAD here    └─────┘         (use y)              │
│    )                                     (free y))           │
│                                                              │
│  Sender loses ownership → Receiver gains ownership           │
└─────────────────────────────────────────────────────────────┘
```

**Key insight:** Channel send = ownership transfer (like Rust's `move`).

#### Implementation Tasks

| Task | Files | Effort |
|------|-------|--------|
| `TChan` type (channel value) | `pkg/ast/value.go` | 2 hrs |
| `TProcess` type (green thread) | `pkg/ast/value.go` | 2 hrs |
| Channel data structure | `pkg/eval/channel.go` | 4 hrs |
| Scheduler (run queue, parking) | `pkg/eval/scheduler.go` | 12 hrs |
| `chan`, `close!` primitives | `pkg/eval/primitives.go` | 2 hrs |
| `>!`, `<!` with parking | `pkg/eval/channel.go` | 8 hrs |
| `go` form (spawn + continuation) | `pkg/eval/eval.go` | 8 hrs |
| `select` form | `pkg/eval/eval.go` | 12 hrs |
| `timeout` primitive | `pkg/eval/primitives.go` | 4 hrs |
| Ownership transfer analysis | `pkg/analysis/ownership.go` | 8 hrs |
| C codegen: scheduler | `pkg/codegen/scheduler.go` | 16 hrs |
| C codegen: channels | `pkg/codegen/channel.go` | 12 hrs |
| Tests | `pkg/eval/channel_test.go` | 8 hrs |

**Deliverable:** Full CSP concurrency with green threads and memory-safe ownership.

#### Example Programs

```scheme
;; Producer-Consumer
(let ((ch (chan 10)))
  (go (let loop ((i 0))
        (when (< i 100)
          (>! ch i)
          (loop (+ i 1)))))
  (go (let loop ()
        (let ((v (<! ch)))
          (when v
            (display v)
            (loop))))))

;; Select with timeout
(let ((ch1 (chan))
      (ch2 (chan)))
  (select
    ((<! ch1) => val (list 'from-ch1 v))
    ((<! ch2) => val (list 'from-ch2 v))
    ((<! (timeout 1000)) 'timeout)))

;; Parallel map
(define (pmap f xs)
  (let ((chs (map (lambda (x)
                    (let ((ch (chan)))
                      (go (>! ch (f x)))
                      ch))
                  xs)))
    (map <! chs)))
```

#### C Runtime: Green Thread Scheduler

```c
/* pkg/codegen/runtime.go additions */

/* Process = green thread */
typedef struct Process {
    void* continuation;       /* CPS continuation function */
    void* env;               /* Continuation environment */
    int state;               /* RUNNING, PARKED, DONE */
    struct Process* next;    /* Linked list */
} Process;

/* Scheduler state */
typedef struct Scheduler {
    Process* run_queue_head;
    Process* run_queue_tail;
    pthread_mutex_t mutex;
    int process_count;
} Scheduler;

/* Channel with wait queues */
typedef struct PurpleChan {
    pthread_mutex_t mutex;
    void** buffer;
    int capacity;
    int size;
    int head;
    int tail;
    int closed;
    Process* send_waiters;   /* Processes waiting to send */
    Process* recv_waiters;   /* Processes waiting to receive */
} PurpleChan;

/* Scheduler API */
void sched_init(void);
void sched_spawn(void* cont, void* env);
void sched_yield(void);
void sched_park(PurpleChan* ch, int is_send);
void sched_run(void);  /* Main loop */

/* Channel API */
PurpleChan* chan_new(int capacity);
void chan_close(PurpleChan* ch);
void chan_send(PurpleChan* ch, void* val);   /* Parks if needed */
void* chan_recv(PurpleChan* ch);             /* Parks if needed */

/* Select */
typedef struct SelectCase {
    PurpleChan* ch;
    int is_send;
    void* val;
} SelectCase;
int chan_select(SelectCase* cases, int n);  /* Returns index of ready case */
```

---

### Phase 6: Polish and Optimization (Week 7)

| Task | Files | Effort |
|------|-------|--------|
| Reify/reflect API | `pkg/eval/primitives.go` | 4 hrs |
| Exception landing pads | `pkg/codegen/exception.go` | 8 hrs |
| Interprocedural analysis | `pkg/analysis/interproc.go` | 12 hrs |
| DPS optimization | `pkg/codegen/dps.go` | 12 hrs |
| Comprehensive tests | Various | 8 hrs |

**Deliverable:** Production-quality Purple implementation.

---

## Part 4: Validation Criteria

### Spec Compliance Tests

From the POPL paper, these examples must work:

```scheme
;; 1. Basic tower access
(EM (+ 1 2))  ;; => 3

;; 2. Meta-level modification
(EM (set! base-eval
      (let ((old-eval base-eval))
        (lambda (e env)
          (display "eval: ") (display e) (newline)
          (old-eval e env)))))
;; Now all evaluation is traced

;; 3. Compile under modified semantics
(clambda (x) (+ x 1))  ;; Generates code respecting current semantics

;; 4. Shift to arbitrary level
(shift 2 (+ 1 1))  ;; Evaluate at level 2

;; 5. Persistent handlers
(set-meta! 'lit (lambda (e menv) ...))  ;; Modify literal handler
```

### Performance Targets

| Metric | Target | Current |
|--------|--------|---------|
| Eval overhead vs native | <10x | ~5x ✅ |
| Code gen quality | Near-C | Good ✅ |
| Tower traversal | O(1) per level | O(1) ✅ |
| Memory (no cycles) | Zero overhead | Zero ✅ |

---

## Part 5: Architecture Alignment

### Current vs Paper Architecture

```
Paper (Scala/LMS):                    Purple Go:
─────────────────                     ──────────
trait Ops[R[_]]                       DefaultHandlers[9]
  - OpsNoRep (interpret)                - Native functions
  - OpsRep (generate)                   - Closure handlers

eval.scala                            pkg/eval/eval.go
  - base_eval                           - Eval()
  - base_apply                          - apply()
  - reify/reflect                       - (implicit)

stage.scala                           pkg/codegen/
  - LMS instantiation                   - CodeGenerator
  - Code[R] type                        - TCode values

Continuations:                        Continuations:
  - Cont type                           - ❌ Missing
  - CPS style                           - Direct style

Cells:                                Cells:
  - Cell type                           - ❌ Missing
  - set!/define                         - ❌ Missing
```

### Recommended Refactors

1. **Add continuation-passing style option**
   - Keep direct style as default
   - CPS mode for when continuations needed

2. **Unify handler calling convention**
   - Currently mixed native/closure
   - Make all handlers take `(expr, menv)` consistently

3. **Explicit reification API**
   - Add `reify` and `reflect` primitives
   - Make tower state first-class

---

## Part 6: Dependencies and Risks

### Dependencies

| Feature | Depends On |
|---------|-----------|
| `set!` | Cell type |
| Continuations | CPS transform or effect system |
| `deftype` | TypeRegistry (done) |
| Exception cleanup | Continuations or explicit cleanup |
| CSP channels | pthreads (done), Cell type |
| `go` form | Channels, goroutine scheduler |
| `select` | Channels, condition variables |
| Ownership transfer | Escape analysis (done) |

### Risks

| Risk | Mitigation |
|------|-----------|
| CPS refactor breaks existing code | Extensive test suite (100+ tests) |
| Performance regression | Benchmark suite exists |
| Semantic divergence from paper | Add spec compliance tests |
| Deadlocks in CSP | Timeout support, select with default |
| Race conditions in channels | Use Go's race detector, careful mutex design |
| Ownership confusion at channel boundaries | Static analysis, GenRef for debugging |

---

## Summary

**Current State:** 85% complete, missing mutation, continuations, and concurrency.

**Critical Path:**
1. Mutable state (`set!`, cells) - Enables Black semantics
2. I/O primitives - Enables practical programs
3. deftype - Enables user-defined types with auto weak-edges
4. **Continuations** - PREREQUISITE for CSP
5. **CSP Concurrency** - Built on continuations, green threads + channels

**Estimated Total Effort:** 6-7 weeks for full implementation.

| Phase | Focus | Weeks | Depends On |
|-------|-------|-------|------------|
| 1 | Mutable State | 1 | - |
| 2 | I/O | 0.5 | - |
| 3 | deftype | 1 | - |
| 4 | **Continuations** | 1.5 | Phase 1 (cells) |
| 5 | **CSP Concurrency** | 2 | **Phase 4 (continuations)** |
| 6 | Polish | 1 | - |

```
Dependency Graph:

  Phase 1 (Cells) ──────┐
                        ├──▶ Phase 4 (Continuations) ──▶ Phase 5 (CSP)
  Phase 2 (I/O)         │
                        │
  Phase 3 (deftype) ────┘
```

**Key Insight:** CSP requires continuations because `go` blocks must be able
to **park** (suspend on channel operations) and **resume** later. This is
fundamentally a continuation capture operation.

**Immediate Next Step:** Implement `TCell` type and `set!` form.

---

## References

### Tower of Interpreters
- [Collapsing Towers of Interpreters (PDF)](https://www.cs.purdue.edu/homes/rompf/papers/amin-popl18.pdf)
- [LMS-Black GitHub](https://github.com/namin/lms-black)
- [SIGPLAN Blog: Reflective Towers](https://blog.sigplan.org/2021/08/12/reflective-towers-of-interpreters/)
- [Asai's Black Language](http://pllab.is.ocha.ac.jp/~asai/Black/)

### CSP and Concurrency
- [Hoare's CSP Book (PDF)](https://www.cs.cmu.edu/~crary/819-f09/Hoare78.pdf)
- [Clojure core.async](https://github.com/clojure/core.async)
- [Manifold (Clojure)](https://github.com/clj-commons/manifold)
- [Manifold vs core.async Comparison](https://andreyor.st/posts/2023-01-09-comparison-of-manifold-and-clojurecoreasync/)
- [A LISP Implementation of CSP (Fidge 1988)](https://onlinelibrary.wiley.com/doi/abs/10.1002/spe.4380181002)
