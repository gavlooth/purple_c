#define _POSIX_C_SOURCE 200809L
#include "dstring.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>

#define DS_INITIAL_CAP 64

// Create empty dynamic string
DString* ds_new(void) {
    return ds_with_capacity(DS_INITIAL_CAP);
}

// Create with initial capacity
DString* ds_with_capacity(size_t cap) {
    DString* ds = malloc(sizeof(DString));
    if (!ds) return NULL;

    if (cap < 16) cap = 16;
    ds->data = malloc(cap);
    if (!ds->data) {
        free(ds);
        return NULL;
    }

    ds->data[0] = '\0';
    ds->len = 0;
    ds->capacity = cap;
    return ds;
}

// Create from C string
DString* ds_from(const char* s) {
    if (!s) return ds_new();

    size_t len = strlen(s);
    size_t cap = len + 1;
    if (cap < DS_INITIAL_CAP) cap = DS_INITIAL_CAP;

    DString* ds = ds_with_capacity(cap);
    if (!ds) return NULL;

    memcpy(ds->data, s, len + 1);
    ds->len = len;
    return ds;
}

// Free dynamic string
void ds_free(DString* ds) {
    if (!ds) return;
    free(ds->data);
    free(ds);
}

// Get C string pointer
const char* ds_cstr(DString* ds) {
    return ds ? ds->data : "";
}

// Take ownership of internal buffer
char* ds_take(DString* ds) {
    if (!ds) return NULL;
    char* result = ds->data;
    free(ds);
    return result;
}

// Clear contents
void ds_clear(DString* ds) {
    if (!ds) return;
    ds->len = 0;
    ds->data[0] = '\0';
}

// Get length
size_t ds_len(DString* ds) {
    return ds ? ds->len : 0;
}

// Ensure capacity - returns 1 on success, 0 on failure
int ds_ensure_capacity(DString* ds, size_t cap) {
    if (!ds) return 0;
    if (ds->capacity >= cap) return 1;

    // Prevent integer overflow in capacity doubling
    size_t new_cap = ds->capacity;
    while (new_cap < cap) {
        if (new_cap > SIZE_MAX / 2) {
            new_cap = cap;  // Just use exact size if we'd overflow
            break;
        }
        new_cap *= 2;
    }

    char* new_data = realloc(ds->data, new_cap);
    if (!new_data) return 0;  // Allocation failed

    ds->data = new_data;
    ds->capacity = new_cap;
    return 1;
}

// Append C string
void ds_append(DString* ds, const char* s) {
    if (!ds || !s) return;
    ds_append_len(ds, s, strlen(s));
}

// Append single character
void ds_append_char(DString* ds, char c) {
    if (!ds) return;
    if (!ds_ensure_capacity(ds, ds->len + 2)) return;  // Check for failure
    ds->data[ds->len++] = c;
    ds->data[ds->len] = '\0';
}

// Append with length
void ds_append_len(DString* ds, const char* s, size_t len) {
    if (!ds || !s || len == 0) return;

    if (!ds_ensure_capacity(ds, ds->len + len + 1)) return;  // Check for failure
    memcpy(ds->data + ds->len, s, len);
    ds->len += len;
    ds->data[ds->len] = '\0';
}

// Append integer
void ds_append_int(DString* ds, long i) {
    if (!ds) return;
    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%ld", i);
    if (len < 0 || (size_t)len >= sizeof(buf)) return;  // Error or truncation
    ds_append_len(ds, buf, (size_t)len);
}

// Printf-style append
void ds_printf(DString* ds, const char* fmt, ...) {
    if (!ds || !fmt) return;

    va_list args, args_copy;
    va_start(args, fmt);
    va_copy(args_copy, args);

    // Calculate needed size
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0) {
        va_end(args_copy);
        return;
    }

    size_t needed_sz = (size_t)needed;
    if (!ds_ensure_capacity(ds, ds->len + needed_sz + 1)) {
        va_end(args_copy);
        return;  // Allocation failed
    }

    vsnprintf(ds->data + ds->len, needed_sz + 1, fmt, args_copy);
    ds->len += needed_sz;
    va_end(args_copy);
}
