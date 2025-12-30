#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/types.h"
#include "src/parser/parser.h"

Value* NIL = NULL;
Value* SYM_QUOTE = NULL;

int main(void) {
    const int depth = 100000;
    const size_t len = (size_t)depth * 2 + 2;
    char* buf = malloc(len);
    if (!buf) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }

    memset(buf, '(', depth);
    buf[depth] = '0';
    memset(buf + depth + 1, ')', depth);
    buf[len - 1] = '\0';

    set_parse_input(buf);
    Value* expr = parse();
    free(buf);

    if (!expr) {
        fprintf(stderr, "parse returned NULL at depth %d\n", depth);
        return 1;
    }

    return 0;
}
