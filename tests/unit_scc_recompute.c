#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/memory/scc.h"

static Obj* mk_node(void) {
    Obj* o = malloc(sizeof(Obj));
    memset(o, 0, sizeof(Obj));
    o->scc_id = -1;
    o->is_pair = 1;
    return o;
}

int main(void) {
    SCCRegistry* reg = mk_scc_registry();

    Obj* n1 = mk_node();
    Obj* n2 = mk_node();

    n1->a = n2;
    n2->a = NULL;

    // First compute (acyclic)
    compute_sccs(reg, n1);

    // Mutate to form a cycle
    n2->a = n1;

    SCC* sccs = compute_sccs(reg, n1);
    if (!sccs) {
        fprintf(stderr, "expected SCCs after mutation, got NULL\n");
        return 1;
    }

    if (sccs->member_count < 2) {
        fprintf(stderr, "expected cycle SCC size >= 2, got %d\n", sccs->member_count);
        return 1;
    }

    return 0;
}
