#ifndef PURPLE_CODEGEN_H
#define PURPLE_CODEGEN_H

#include "../types.h"
#include "../analysis/shape.h"
#include "../analysis/escape.h"

// -- Code Generation --

// Emit C code for a value
Value* emit_c_call(const char* fn, Value* a, Value* b);
Value* lift_value(Value* v);
char* val_to_c_expr(Value* v);

// ASAP scanner generation
void gen_asap_scanner(const char* type_name, int is_list);

// Field-aware scanner generation
void gen_field_aware_scanner(const char* type_name);

// Type definition generation
typedef enum {
    FIELD_STRONG,
    FIELD_WEAK,
    FIELD_UNTRACED
} FieldStrength;

typedef struct TypeField {
    char* name;
    char* type;
    int is_scannable;
    FieldStrength strength;
} TypeField;

typedef struct TypeDef {
    char* name;
    TypeField* fields;
    int field_count;
    int is_recursive;
    struct TypeDef* next;
} TypeDef;

// Type registry
extern TypeDef* TYPE_REGISTRY;

void register_type(const char* name, TypeField* fields, int count);
TypeDef* find_type(const char* name);
void init_type_registry(void);

// Ownership graph for back-edge detection
typedef struct OwnershipEdge {
    char* from_type;
    char* field_name;
    char* to_type;
    int is_back_edge;
    struct OwnershipEdge* next;
} OwnershipEdge;

void build_ownership_graph(void);
void analyze_back_edges(void);

// Struct and release function generation
void gen_struct_def(TypeDef* t);
void gen_release_func(TypeDef* t);
void gen_weak_ref_runtime(void);

// Perceus reuse
typedef struct ReusePair {
    void* free_site;
    void* alloc_site;
    char* reuse_var;
    struct ReusePair* next;
} ReusePair;

void gen_perceus_runtime(void);
void gen_reuse_alloc(ReusePair* pair, char* buf, int buf_size);

// NLL free point generation
typedef struct FreePoint {
    char* var_name;
    int node_id;
    int is_conditional;
    struct FreePoint* next;
} FreePoint;

void gen_nll_free(FreePoint* fp, char* buf, int buf_size);

// Runtime header generation
void gen_runtime_header(void);

#endif // PURPLE_CODEGEN_H
