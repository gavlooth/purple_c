#ifndef PURPLE_PARSER_H
#define PURPLE_PARSER_H

#include "../types.h"

// -- Reader/Parser --

// Set input string
void set_parse_input(const char* input);

// Parse a single expression
Value* parse(void);

// Parse helpers
void skip_ws(void);
Value* parse_list(void);

#endif // PURPLE_PARSER_H
