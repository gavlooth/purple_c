#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "../eval/eval.h"
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

extern Value* NIL;
extern Value* SYM_QUOTE;

// -- Parser State --

static const char* parse_ptr = NULL;

void set_parse_input(const char* input) {
    parse_ptr = input;
}

// -- Parsing --

void skip_ws(void) {
    while (parse_ptr && isspace((unsigned char)*parse_ptr)) parse_ptr++;
}

// Iterative Parser Context Frame
typedef struct ParseFrame {
    Value* list;           // Accumulator for list elements (built in reverse)
    int closing_char;      // ')' or 0 (for quotes)
    int max_items;         // -1 (unlimited) or N (for quotes)
    int items_read;        // Number of items read so far
    struct ParseFrame* next;
} ParseFrame;

static void push_frame(ParseFrame** stack, Value* initial_list, int closing, int max) {
    ParseFrame* f = malloc(sizeof(ParseFrame));
    if (!f) {
        fprintf(stderr, "Parser OOM\n");
        exit(1);
    }
    f->list = initial_list;
    f->closing_char = closing;
    f->max_items = max;
    f->items_read = 0;
    f->next = *stack;
    *stack = f;
}

static ParseFrame* pop_frame(ParseFrame** stack) {
    if (!*stack) return NULL;
    ParseFrame* f = *stack;
    *stack = f->next;
    return f;
}

static Value* reverse_list(Value* list) {
    Value* new_head = NIL;
    while (!is_nil(list)) {
        Value* next = cdr(list);
        list->cell.cdr = new_head;
        new_head = list;
        list = next;
    }
    return new_head;
}

Value* parse(void) {
    ParseFrame* stack = NULL;
    Value* current_result = NULL;
    int result_ready = 0;

    // Main Loop
    while (1) {
        skip_ws();
        if (!parse_ptr || *parse_ptr == '\0') break;

        // Check if current frame is done (for max_items frames like Quote)
        if (stack && stack->max_items != -1 && stack->items_read >= stack->max_items) {
            // Frame finished
            current_result = reverse_list(stack->list);
            ParseFrame* f = pop_frame(&stack);
            free(f);
            result_ready = 1;
        } 
        // Check for closing parenthesis (only if frame expects it)
        else if (stack && stack->closing_char && *parse_ptr == stack->closing_char) {
            parse_ptr++; // Consume ')'
            current_result = reverse_list(stack->list);
            ParseFrame* f = pop_frame(&stack);
            free(f);
            result_ready = 1;
        }
        else {
            // Need to read next item
            if (*parse_ptr == '(') {
                parse_ptr++;
                push_frame(&stack, NIL, ')', -1);
                continue;
            }
            else if (*parse_ptr == '\'') {
                parse_ptr++;
                // Quote expands to (quote <next>)
                // We create a list (quote) and expect 1 more item
                Value* q = mk_cell(SYM_QUOTE ? SYM_QUOTE : mk_sym("quote"), NIL);
                push_frame(&stack, q, 0, 1);
                continue;
            }
            else if (*parse_ptr == ')') {
                // Unexpected closing parenthesis (if stack empty or mismatch)
                if (!stack) {
                    parse_ptr++;
                    return NIL; // Or error? Original parser returned NIL
                }
                // If mismatch, we let the closing check above handle it or consume loop
                // But wait, the check above only fires if closing_char matches.
                // If we are in a quote frame (closing=0) and see ')', we should close the quote frame?
                // No, quote expects exactly 1 item. ) is not an item.
                // This implies malformed input: ' )
                // Original parser behavior on ')': return NIL.
                // We should probably just break or return NIL if we see unexpected )
                // But let's assume valid input for now or minimal error handling.
                 if (stack && stack->closing_char != ')') {
                    // We are in a quote or limited frame, but hit ')'.
                    // This means the quote ended early?
                    // e.g. (list ' )
                    // The ' expects an atom or list. ) ends the outer list.
                    // This is invalid.
                    // Let's just consume it and let the outer frame handle it.
                    // But we can't consume it here if it belongs to outer.

                    // Actually, if we see ')' and we are expecting an item (Quote), it's an error.
                    // But to be robust, maybe we just close the quote frame as is?
                    current_result = reverse_list(stack->list);
                    ParseFrame* f = pop_frame(&stack);
                    free(f);
                    result_ready = 1;
                    // Don't consume ')' yet, let next loop iteration handle it for parent
                 } else {
                     // Should be handled by closing check above
                     // If we are here, stack->closing_char != ')' or something.
                     parse_ptr++;
                     // Free remaining stack frames before returning
                     while (stack) {
                         ParseFrame* f = pop_frame(&stack);
                         free(f);
                     }
                     return NIL;
                 }
            }
            else {
                // Atom (int or sym)
                if (isdigit((unsigned char)*parse_ptr) || (*parse_ptr == '-' && isdigit((unsigned char)parse_ptr[1]))) {
                    errno = 0;
                    char* endptr;
                    long i = strtol(parse_ptr, &endptr, 10);
                    if (errno == ERANGE || (errno != 0 && i == 0)) {
                        fprintf(stderr, "Parse error: integer overflow\n");
                        // Consume the invalid token and return NIL
                        parse_ptr = endptr;
                        current_result = NIL;
                    } else {
                        parse_ptr = endptr;
                        current_result = mk_int(i);
                    }
                } else {
                    const char* start = parse_ptr;
                    while (*parse_ptr && !isspace((unsigned char)*parse_ptr) && *parse_ptr != ')' && *parse_ptr != '(') {
                        parse_ptr++;
                    }
                    char* s = strndup(start, parse_ptr - start);
                    if (!s) {
                        fprintf(stderr, "Parser OOM\n");
                        while (stack) {
                            ParseFrame* f = pop_frame(&stack);
                            free(f);
                        }
                        return NULL;
                    }
                    current_result = mk_sym(s);
                    free(s);
                }
                result_ready = 1;
            }
        }

        // If we produced a result, append to stack or return
        if (result_ready) {
            result_ready = 0;
            if (!stack) {
                return current_result;
            } else {
                stack->list = mk_cell(current_result, stack->list);
                stack->items_read++;
                current_result = NULL;
            }
        }
    }

    // Free remaining stack frames on early exit (e.g., unclosed parentheses)
    while (stack) {
        ParseFrame* f = pop_frame(&stack);
        free(f);
    }

    return NULL;
}

// Deprecated recursion entry point (kept for header compat if needed, but parse() covers it)
Value* parse_list(void) {
    if (!parse_ptr) return NIL;
    if (*parse_ptr == '(') {
        parse_ptr++;
        return parse(); // parse now handles the loop
    }
    return NIL;
}