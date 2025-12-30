#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "memory/scc.h"

static Obj* mk_node(void) {
    Obj* o = malloc(sizeof(Obj));
    memset(o, 0, sizeof(Obj));
    o->scc_id = -1;
    return o;
}

int main(void) {
    SCCRegistry* reg = mk_scc_registry();

    Obj* n1 = mk_node();
    Obj* n2 = mk_node();
    Obj* n3 = mk_node();

    // Graph: n1 -> n2, n1 -> n3, n3 -> n2
    n1->a = n2;
    n1->b = n3;
    n3->a = n2;

    SCC* sccs = compute_sccs(reg, n1);

    int count = 0;
    for (SCC* s = sccs; s; s = s->next) count++;

    if (count != 3) {
        fprintf(stderr, "expected 3 SCCs, got %d\n", count);
        return 1;
    }

    return 0;
}
