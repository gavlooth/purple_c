#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>

// Mock parser components
typedef struct Value { int dummy; } Value;
Value* NIL = NULL;
Value* mk_cell(Value* a, Value* b) { return malloc(sizeof(Value)); }
Value* mk_int(long i) { return malloc(sizeof(Value)); }
Value* mk_sym(const char* s) { return malloc(sizeof(Value)); }
Value* parse();

const char* parse_ptr = NULL;
void skip_ws() { while(parse_ptr && isspace(*parse_ptr)) parse_ptr++; }

Value* parse_list() {
    skip_ws();
    if (*parse_ptr == ')') {
        parse_ptr++;
        return NIL;
    }
    Value* head = parse(); // Recursive call
    Value* tail = parse_list(); // Recursive call
    return mk_cell(head, tail);
}

Value* parse() {
    skip_ws();
    if (!parse_ptr || *parse_ptr == '\0') return NULL;
    if (*parse_ptr == '(') {
        parse_ptr++;
        return parse_list();
    }
    // ... simplifed ...
    if (*parse_ptr == ')') return NULL; 
    while (*parse_ptr && !isspace(*parse_ptr) && *parse_ptr != ')') parse_ptr++;
    return mk_int(0);
}

int main() {
    // Generate 50000 nested parens
    int depth = 50000;
    char* buf = malloc(depth + 10);
    for(int i=0; i<depth; i++) buf[i] = '(';
    buf[depth] = '0';
    buf[depth+1] = 0;
    
    parse_ptr = buf;
    printf("Parsing depth %d...\n", depth);
    parse();
    printf("Done.\n");
    return 0;
}
