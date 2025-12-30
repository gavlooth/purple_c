# Bug Report - Purple C Compiler

This document tracks low-level bugs identified through static analysis.

## Summary

| Category | Count | Severity | Status |
|----------|-------|----------|--------|
| Unchecked malloc | 13 | CRITICAL | FIXED |
| Unchecked strdup | 22 | CRITICAL | FIXED |
| Unsafe realloc | 3 | CRITICAL | FIXED |
| Silent capacity failure | 2 | HIGH | FIXED |
| Integer overflow | 1 | HIGH | FIXED |
| NULL dereference | 4 | HIGH | FIXED |
| Missing cleanup | 2 | MEDIUM | FIXED |
| Generated code | 2 | MEDIUM | FIXED |

---

## CRITICAL: Unchecked malloc

### Issue
`malloc()` can return NULL on allocation failure. Dereferencing NULL causes segfault.

### Affected Files

| File | Line | Variable |
|------|------|----------|
| `src/memory/scc.c` | 7 | `SCCRegistry*` |
| `src/memory/scc.c` | 48 | `SCC*` |
| `src/memory/scc.c` | 89 | `SCCNode*` |
| `src/memory/scc.c` | 157 | `TarjanFrame*` |
| `src/memory/deferred.c` | 7 | `DeferredContext*` |
| `src/memory/deferred.c` | 43 | `DeferredDec*` |
| `src/memory/arena.c` | 8 | `Arena*` |
| `src/memory/arena.c` | 27 | `ArenaBlock*` |
| `src/memory/exception.c` | 15 | `LiveAlloc*` |
| `src/memory/exception.c` | 50 | `LandingPad*` |
| `src/memory/exception.c` | 60 | `CleanupAction*` |
| `src/parser/parser.c` | 29 | `ParseStack*` |
| `src/analysis/escape.c` | 10, 41 | `AnalysisContext*`, `VarUsage*` |
| `src/analysis/shape.c` | 21, 50 | `ShapeContext*`, `ShapeInfo*` |
| `src/codegen/codegen.c` | 84, 120, 148 | `TypeDef*`, `OwnershipEdge*`, `VisitState*` |
| `src/eval/eval.c` | 166 | `BindingInfo*` |
| `src/util/hashmap.c` | 25, 125 | `HashMap*`, `HashEntry*` |

### Fix
Add NULL check after every malloc:
```c
Type* ptr = malloc(sizeof(Type));
if (!ptr) return NULL;  // or handle error appropriately
```

---

## CRITICAL: Unchecked strdup

### Issue
`strdup()` calls malloc internally and can return NULL.

### Affected Files

| File | Line | Context |
|------|------|---------|
| `src/types.c` | 22, 41 | `mk_sym`, `mk_code` |
| `src/memory/exception.c` | 16, 17, 61, 65 | `track_alloc`, `create_landing_pad` |
| `src/analysis/shape.c` | 51 | `track_shape` |
| `src/analysis/escape.c` | 42, 213 | `track_var`, `find_free_vars` |
| `src/codegen/codegen.c` | 85, 121-123, 149 | `register_type`, `find_ownership_edges` |

### Fix
Check strdup return value:
```c
char* s = strdup(str);
if (!s) { /* handle error */ }
```

---

## CRITICAL: Unsafe realloc Pattern

### Issue
When `realloc()` fails, it returns NULL but the original pointer is NOT freed.
If you assign directly: `ptr = realloc(ptr, size)`, you lose the original pointer.

### Affected Locations

1. **src/analysis/escape.c:212**
   ```c
   *free_vars = realloc(*free_vars, (*count + 1) * sizeof(char*));
   ```

2. **src/memory/scc.c:63**
   ```c
   scc->members = realloc(scc->members, scc->capacity * sizeof(Obj*));
   ```

3. **src/memory/exception.c:145** (generated code)

### Fix
Use temporary variable:
```c
void* tmp = realloc(ptr, new_size);
if (!tmp) return;  // Keep original ptr valid
ptr = tmp;
```

---

## HIGH: Silent Capacity Failure in DString

### Issue
`ds_ensure_capacity()` returns silently on realloc failure, but callers assume
capacity was increased and write beyond buffer bounds.

### Location
`src/util/dstring.c:82-93`

```c
void ds_ensure_capacity(DString* ds, size_t cap) {
    // ...
    char* new_data = realloc(ds->data, new_cap);
    if (!new_data) return;  // Silent failure!
    ds->data = new_data;
    ds->capacity = new_cap;
}
```

### Impact
Buffer overflow in `ds_append_char()`, `ds_append_len()`, `ds_printf()`.

### Fix
Return error status and check in callers, or abort on OOM.

---

## HIGH: Integer Overflow in Capacity Doubling

### Issue
```c
size_t new_cap = ds->capacity * 2;
while (new_cap < cap) new_cap *= 2;
```

If `capacity > SIZE_MAX/2`, multiplication overflows and wraps to small value.

### Location
`src/util/dstring.c:85-86`

### Fix
Check for overflow before doubling:
```c
if (new_cap > SIZE_MAX / 2) {
    new_cap = SIZE_MAX;
} else {
    new_cap *= 2;
}
```

---

## HIGH: NULL Dereference in eval.c

### Issue
`menv` parameter not checked before dereferencing.

### Location
`src/eval/eval.c:318`

```c
if (expr->tag == T_INT) return menv->menv.h_lit(expr, menv);
```

### Fix
Add NULL check:
```c
if (!menv) return NIL;
```

---

## MEDIUM: Missing Error Path Cleanup in Parser

### Issue
If parsing fails midway through `parse_list()`, stack elements are leaked.

### Location
`src/parser/parser.c:45-66`

### Fix
Add cleanup on error paths.

---

## MEDIUM: Generated Code Issues

### Issue 1: Division NULL check order
```c
printf("Obj* div_op(Obj* a, Obj* b) { return mk_int(b->i ? a->i / b->i : 0); }\n");
```
If `b` is NULL, `b->i` crashes before the ternary check.

### Issue 2: INT_MIN % -1 undefined behavior
```c
printf("Obj* mod_op(Obj* a, Obj* b) { return mk_int(b->i ? a->i %% b->i : 0); }\n");
```

### Location
`src/main.c` (or wherever primitives are generated)

### Fix
Add NULL checks and edge case handling in generated code.

---

## Testing

Unit tests for these fixes are in `tests/unit_memory.c`.
