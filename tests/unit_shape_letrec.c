#include <stdio.h>
#include "src/analysis/shape.h"

static Value* nil(void) {
    return NULL;
}

int main(void) {
    // (letrec ((x (cons 1 x))) x)
    Value* sym_letrec = mk_sym("letrec");
    Value* sym_x = mk_sym("x");
    Value* sym_cons = mk_sym("cons");

    Value* cons_expr = mk_cell(sym_cons,
                               mk_cell(mk_int(1),
                                       mk_cell(sym_x, nil())));
    Value* binding = mk_cell(sym_x, mk_cell(cons_expr, nil()));
    Value* bindings = mk_cell(binding, nil());
    Value* expr = mk_cell(sym_letrec, mk_cell(bindings, mk_cell(sym_x, nil())));

    ShapeContext* ctx = mk_shape_context();
    analyze_shapes_expr(expr, ctx);

    ShapeInfo* info = find_shape(ctx, "x");
    if (!info || info->shape != SHAPE_CYCLIC) {
        fprintf(stderr, "expected x to be SHAPE_CYCLIC, got %s\n",
                info ? shape_to_string(info->shape) : "NULL");
        return 1;
    }

    return 0;
}
