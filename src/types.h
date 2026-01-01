#ifndef PURPLE_TYPES_H
#define PURPLE_TYPES_H

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// -- Core Value Types --

typedef enum {
    T_INT, T_SYM, T_CELL, T_NIL, T_PRIM, T_MENV, T_CODE, T_LAMBDA,
    T_ERROR,    // Error value
    T_BOX,      // Mutable reference cell
    T_CONT,     // First-class continuation
    T_CHAN,     // CSP channel
    T_PROCESS   // Green thread / process
} Tag;

struct Value;

// Function pointer types
typedef struct Value* (*PrimFn)(struct Value* args, struct Value* menv);
typedef struct Value* (*HandlerFn)(struct Value* exp, struct Value* menv);

// Forward declarations for new types
struct Channel;
struct Scheduler;
typedef struct Value* (*ContFn)(struct Value* val);

// Process states
#define PROC_READY   0
#define PROC_RUNNING 1
#define PROC_PARKED  2
#define PROC_DONE    3

// Channel structure for CSP (cooperative, continuation-based)
typedef struct Channel {
    struct Value** buffer;      // Circular buffer for buffered channels
    int capacity;               // 0 for unbuffered
    int head;
    int tail;
    int count;
    int closed;
    struct Value* send_waiters; // List of (process . value) waiting to send
    struct Value* recv_waiters; // List of processes waiting to receive
} Channel;

// Continuation escape structure (for setjmp/longjmp)
#include <setjmp.h>
typedef struct ContEscape {
    jmp_buf env;
    struct Value* result;
    int tag;
    int active;
} ContEscape;

// Prompt stack for delimited continuations
#define MAX_PROMPT_DEPTH 64

// Core Value structure
typedef struct Value {
    Tag tag;
    union {
        long i;                          // T_INT
        char* s;                         // T_SYM, T_CODE, T_ERROR
        struct { struct Value* car; struct Value* cdr; } cell;  // T_CELL
        PrimFn prim;                     // T_PRIM
        struct {                         // T_MENV
            struct Value* env;
            struct Value* parent;
            HandlerFn h_app;
            HandlerFn h_let;
            HandlerFn h_if;
            HandlerFn h_lit;
            HandlerFn h_var;
        } menv;
        struct {                         // T_LAMBDA
            struct Value* params;
            struct Value* body;
            struct Value* env;
        } lam;
        struct Value* box_value;         // T_BOX - mutable reference cell
        struct {                         // T_CONT - continuation
            ContFn fn;
            struct Value* menv;
            int tag;                     // Unique tag for matching
        } cont;
        struct {                         // T_CHAN - channel
            struct Channel* ch;
            int capacity;
        } chan;
        struct {                         // T_PROCESS - green thread
            struct Value* thunk;         // Lambda to execute
            struct Value* cont;          // Saved continuation for resuming
            struct Value* menv;          // Saved meta-environment
            struct Value* result;        // Final result when done
            struct Value* park_value;    // Value for park/unpark
            int state;
        } proc;
    };
} Value;

// -- Value Constructors --
Value* alloc_val(Tag tag);
Value* mk_int(long i);
Value* mk_sym(const char* s);
Value* mk_cell(Value* car, Value* cdr);
Value* mk_prim(PrimFn fn);
Value* mk_code(const char* s);
Value* mk_lambda(Value* params, Value* body, Value* env);
Value* mk_error(const char* msg);
Value* mk_box(Value* initial);
Value* mk_cont(ContFn fn, Value* menv, int tag);
Value* mk_chan(int capacity);
Value* mk_process(Value* thunk);

// -- Type Predicates --
int is_box(Value* v);
int is_cont(Value* v);
int is_chan(Value* v);
int is_process(Value* v);
int is_error(Value* v);

// -- Box Operations --
Value* box_get(Value* box);
void box_set(Value* box, Value* val);

// -- Value Helpers --
int is_nil(Value* v);
int is_code(Value* v);
Value* car(Value* v);
Value* cdr(Value* v);
char* val_to_str(Value* v);
char* list_to_str(Value* v);

// Symbol comparison
int sym_eq(Value* s1, Value* s2);
int sym_eq_str(Value* s1, const char* s2);

// -- Compiler Arena (Phase 12) --
// All compiler allocations use this arena for bulk deallocation
void compiler_arena_init(void);
void compiler_arena_cleanup(void);
void compiler_arena_register_string(char* s);

// -- List Construction --
#define LIST1(a) mk_cell(a, NIL)
#define LIST2(a,b) mk_cell(a, mk_cell(b, NIL))
#define LIST3(a,b,c) mk_cell(a, mk_cell(b, mk_cell(c, NIL)))

#endif // PURPLE_TYPES_H
