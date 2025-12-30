#ifndef PURPLE_DSTRING_H
#define PURPLE_DSTRING_H

#include <stddef.h>

// Dynamic String - sds-style growable string buffer
// Prevents buffer overflow by automatically growing

typedef struct DString {
    char* data;      // Null-terminated string data
    size_t len;      // Current length (excluding null terminator)
    size_t capacity; // Allocated capacity (including null terminator)
} DString;

// Create/destroy
DString* ds_new(void);
DString* ds_from(const char* s);
DString* ds_with_capacity(size_t cap);
void ds_free(DString* ds);

// Get C string (for compatibility)
const char* ds_cstr(DString* ds);
char* ds_take(DString* ds);  // Take ownership of internal buffer, free DString

// Modification
void ds_clear(DString* ds);
void ds_append(DString* ds, const char* s);
void ds_append_char(DString* ds, char c);
void ds_append_len(DString* ds, const char* s, size_t len);
void ds_append_int(DString* ds, long i);
void ds_printf(DString* ds, const char* fmt, ...);

// Utility
size_t ds_len(DString* ds);
int ds_ensure_capacity(DString* ds, size_t cap);  // Returns 1 on success, 0 on failure

#endif // PURPLE_DSTRING_H
