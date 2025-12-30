#ifndef PURPLE_ESCAPE_H
#define PURPLE_ESCAPE_H

#include "../types.h"

// -- Escape Analysis --
// Determines where values may escape to

typedef enum {
    ESCAPE_NONE = 0,    // Can stack-allocate
    ESCAPE_ARG = 1,     // Escapes via function argument
    ESCAPE_GLOBAL = 2,  // Escapes to return/global
} EscapeClass;

typedef struct VarUsage {
    char* name;
    int use_count;
    int last_use_depth;
    int escape_class;
    int captured_by_lambda;
    int freed;
    struct VarUsage* next;
} VarUsage;

typedef struct AnalysisContext {
    VarUsage* vars;
    int current_depth;
    int in_lambda;
} AnalysisContext;

// Context management
AnalysisContext* mk_analysis_ctx(void);
void free_analysis_ctx(AnalysisContext* ctx);
VarUsage* find_var(AnalysisContext* ctx, const char* name);
void add_var(AnalysisContext* ctx, const char* name);
void record_use(AnalysisContext* ctx, const char* name);

// Analysis functions
void analyze_expr(Value* expr, AnalysisContext* ctx);
void analyze_escape(Value* expr, AnalysisContext* ctx, EscapeClass current_escape);

// Capture tracking for closures
void find_free_vars(Value* expr, Value* bound, char*** free_vars, int* count);

// Global context
extern AnalysisContext* g_analysis_ctx;

#endif // PURPLE_ESCAPE_H
