#include "parser.h"
#include "../eval/eval.h"
#include <ctype.h>
#include <string.h>

// -- Parser State --

static const char* parse_ptr = NULL;

void set_parse_input(const char* input) {
    parse_ptr = input;
}

// -- Parsing --

void skip_ws(void) {
    while (parse_ptr && isspace(*parse_ptr)) parse_ptr++;
}

Value* parse_list(void) {
    skip_ws();
    if (*parse_ptr == ')') {
        parse_ptr++;
        return NIL;
    }
    Value* head = parse();
    Value* tail = parse_list();
    return mk_cell(head, tail);
}

Value* parse(void) {
    skip_ws();
    if (!parse_ptr || *parse_ptr == '\0') return NULL;

    if (*parse_ptr == '(') {
        parse_ptr++;
        return parse_list();
    }

    if (*parse_ptr == '\'') {
        parse_ptr++;
        Value* v = parse();
        return mk_cell(SYM_QUOTE, mk_cell(v, NIL));
    }

    if (isdigit(*parse_ptr) || (*parse_ptr == '-' && isdigit(parse_ptr[1]))) {
        long i = strtol(parse_ptr, (char**)&parse_ptr, 10);
        return mk_int(i);
    }

    const char* start = parse_ptr;
    while (*parse_ptr && !isspace(*parse_ptr) && *parse_ptr != ')' && *parse_ptr != '(') {
        parse_ptr++;
    }
    char* s = strndup(start, parse_ptr - start);
    Value* sym = mk_sym(s);
    free(s);
    return sym;
}
