#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/types.h"

Value* NIL = NULL;

int main(void) {
    const int count = 10000;
    Value* list = NULL;

    for (int i = 0; i < count; i++) {
        list = mk_cell(mk_int(1), list);
    }

    char* s = list_to_str(list);
    size_t len = strlen(s);
    size_t expected = (size_t)(count * 2) + 1; // '(' + N digits + (N-1) spaces + ')'

    if (len != expected) {
        fprintf(stderr, "unexpected length: got %zu, expected %zu\n", len, expected);
        free(s);
        return 1;
    }
    if (s[0] != '(' || s[len - 1] != ')') {
        fprintf(stderr, "unexpected delimiters in list string\n");
        free(s);
        return 1;
    }

    free(s);
    return 0;
}
