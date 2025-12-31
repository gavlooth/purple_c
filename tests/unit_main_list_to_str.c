// Unit test for main.c's list_to_str with large lists
// Verifies the DString-based implementation handles large lists without buffer overflow

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Include main.c directly to test its internal list_to_str
#define main main_original
#include "../main.c"
#undef main

int main(void) {
    // Create a list with 500 elements (would overflow 1024-byte buffer)
    // Each element "1" = 1 char, plus space = 2 chars, so 500 * 2 = 1000+ chars
    const int count = 500;
    Value* list = NIL;

    for (int i = 0; i < count; i++) {
        list = mk_cons(mk_int(1), list);
    }

    char* s = list_to_str(list);
    if (!s) {
        fprintf(stderr, "list_to_str returned NULL\n");
        return 1;
    }

    size_t len = strlen(s);
    // Expected: '(' + 500 * "1" + 499 * " " + ')' = 1 + 500 + 499 + 1 = 1001
    size_t expected = 1 + (size_t)count + (size_t)(count - 1) + 1;

    if (len != expected) {
        fprintf(stderr, "unexpected length: got %zu, expected %zu\n", len, expected);
        free(s);
        return 1;
    }

    if (s[0] != '(' || s[len - 1] != ')') {
        fprintf(stderr, "unexpected delimiters: '%c' ... '%c'\n", s[0], s[len-1]);
        free(s);
        return 1;
    }

    printf("Large list stringification OK (len=%zu)\n", len);
    free(s);
    return 0;
}
