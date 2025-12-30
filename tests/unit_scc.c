// Unit tests for scc.c - targeting 90%+ coverage
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "../src/memory/scc.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

// Helper to create test Obj
static Obj* mk_test_int(int val) {
    Obj* o = malloc(sizeof(Obj));
    if (!o) return NULL;
    o->mark = 1;
    o->scc_id = -1;
    o->is_pair = 0;
    o->i = val;
    return o;
}

static Obj* mk_test_pair(Obj* a, Obj* b) {
    Obj* o = malloc(sizeof(Obj));
    if (!o) return NULL;
    o->mark = 1;
    o->scc_id = -1;
    o->is_pair = 1;
    o->a = a;
    o->b = b;
    return o;
}

// Test registry lifecycle
void test_scc_registry_lifecycle(void) {
    TEST(scc_registry_lifecycle);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }
    if (reg->sccs != NULL) { FAIL("sccs should be NULL"); free_scc_registry(reg); return; }
    if (reg->next_id != 1) { FAIL("next_id should be 1"); free_scc_registry(reg); return; }
    if (!reg->node_lookup) { FAIL("node_lookup should exist"); free_scc_registry(reg); return; }

    free_scc_registry(reg);

    // Test free NULL
    free_scc_registry(NULL);

    PASS();
}

// Test create SCC
void test_create_scc(void) {
    TEST(create_scc);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    SCC* scc1 = create_scc(reg);
    if (!scc1) { FAIL("create_scc returned NULL"); free_scc_registry(reg); return; }
    if (scc1->id != 1) { FAIL("first scc id should be 1"); free_scc_registry(reg); return; }
    if (scc1->member_count != 0) { FAIL("member_count should be 0"); free_scc_registry(reg); return; }
    if (scc1->ref_count != 1) { FAIL("ref_count should be 1"); free_scc_registry(reg); return; }

    SCC* scc2 = create_scc(reg);
    if (!scc2) { FAIL("second create_scc returned NULL"); free_scc_registry(reg); return; }
    if (scc2->id != 2) { FAIL("second scc id should be 2"); free_scc_registry(reg); return; }

    // Verify they're in the registry list
    if (reg->sccs != scc2) { FAIL("sccs should point to most recent"); free_scc_registry(reg); return; }
    if (scc2->next != scc1) { FAIL("scc2->next should be scc1"); free_scc_registry(reg); return; }

    free_scc_registry(reg);

    // Create with NULL reg
    SCC* scc3 = create_scc(NULL);
    if (scc3 != NULL) { FAIL("create_scc(NULL) should return NULL"); return; }

    PASS();
}

// Test add to SCC
void test_add_to_scc(void) {
    TEST(add_to_scc);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    SCC* scc = create_scc(reg);
    if (!scc) { FAIL("create_scc returned NULL"); free_scc_registry(reg); return; }

    Obj* obj1 = mk_test_int(1);
    Obj* obj2 = mk_test_int(2);

    add_to_scc(scc, obj1);
    if (scc->member_count != 1) { FAIL("member_count should be 1"); goto cleanup; }
    if (obj1->scc_id != scc->id) { FAIL("obj1 scc_id should be set"); goto cleanup; }

    add_to_scc(scc, obj2);
    if (scc->member_count != 2) { FAIL("member_count should be 2"); goto cleanup; }

    // Test capacity growth
    for (int i = 0; i < 50; i++) {
        Obj* obj = mk_test_int(i + 10);
        add_to_scc(scc, obj);
    }
    if (scc->member_count != 52) { FAIL("member_count should be 52"); goto cleanup; }
    if (scc->capacity < 52) { FAIL("capacity should have grown"); goto cleanup; }

    // NULL inputs
    add_to_scc(NULL, obj1);  // Should not crash
    add_to_scc(scc, NULL);   // Should not crash

    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free_scc_registry(reg);
}

// Test find SCC
void test_find_scc(void) {
    TEST(find_scc);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    SCC* scc1 = create_scc(reg);
    SCC* scc2 = create_scc(reg);
    SCC* scc3 = create_scc(reg);
    (void)scc3;  // suppress warning

    SCC* found = find_scc(reg, scc2->id);
    if (found != scc2) { FAIL("should find scc2"); free_scc_registry(reg); return; }

    found = find_scc(reg, scc1->id);
    if (found != scc1) { FAIL("should find scc1"); free_scc_registry(reg); return; }

    found = find_scc(reg, 999);
    if (found != NULL) { FAIL("should not find non-existent"); free_scc_registry(reg); return; }

    free_scc_registry(reg);
    PASS();
}

// Test SCC ref counting
void test_scc_refcount(void) {
    TEST(scc_refcount);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    SCC* scc = create_scc(reg);
    if (scc->ref_count != 1) { FAIL("initial ref_count should be 1"); free_scc_registry(reg); return; }

    inc_scc_ref(scc);
    if (scc->ref_count != 2) { FAIL("ref_count should be 2"); free_scc_registry(reg); return; }

    inc_scc_ref(scc);
    if (scc->ref_count != 3) { FAIL("ref_count should be 3"); free_scc_registry(reg); return; }

    release_scc(scc);
    if (scc->ref_count != 2) { FAIL("ref_count should be 2 after release"); free_scc_registry(reg); return; }

    // NULL inputs
    inc_scc_ref(NULL);   // Should not crash
    release_scc(NULL);   // Should not crash

    free_scc_registry(reg);
    PASS();
}

// Test single node (no cycle)
void test_scc_single_node(void) {
    TEST(scc_single_node);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    Obj* obj = mk_test_int(42);
    SCC* result = compute_sccs(reg, obj);

    if (!result) { FAIL("result should not be NULL"); goto cleanup; }
    if (result->member_count != 1) { FAIL("should have 1 member"); goto cleanup; }
    if (result->members[0] != obj) { FAIL("member should be obj"); goto cleanup; }

    free(obj);
    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free(obj);
    free_scc_registry(reg);
}

// Test linear chain (no cycle)
void test_scc_linear_chain(void) {
    TEST(scc_linear_chain);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    // Create A -> B -> C
    Obj* c = mk_test_int(3);
    Obj* b = mk_test_pair(c, NULL);
    Obj* a = mk_test_pair(b, NULL);

    SCC* result = compute_sccs(reg, a);

    // Should have 3 separate SCCs (no cycles)
    int count = 0;
    SCC* scc = result;
    while (scc) {
        if (scc->member_count != 1) { FAIL("each SCC should have 1 member"); goto cleanup; }
        count++;
        scc = scc->result_next;
    }
    if (count != 3) { FAIL("should have 3 SCCs"); goto cleanup; }

    free(a); free(b); free(c);
    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free(a); free(b); free(c);
    free_scc_registry(reg);
}

// Test simple cycle A -> B -> A
void test_scc_simple_cycle(void) {
    TEST(scc_simple_cycle);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    // Create A <-> B cycle
    Obj* a = mk_test_pair(NULL, NULL);
    Obj* b = mk_test_pair(a, NULL);
    a->a = b;  // Create cycle

    SCC* result = compute_sccs(reg, a);

    // Should have 1 SCC with 2 members
    if (!result) { FAIL("result should not be NULL"); goto cleanup; }
    if (result->member_count != 2) { FAIL("SCC should have 2 members"); goto cleanup; }
    if (result->result_next != NULL) { FAIL("should only be 1 SCC"); goto cleanup; }

    // Both should have same scc_id
    if (a->scc_id != b->scc_id) { FAIL("a and b should have same scc_id"); goto cleanup; }

    free(a); free(b);
    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free(a); free(b);
    free_scc_registry(reg);
}

// Test self-loop
void test_scc_self_loop(void) {
    TEST(scc_self_loop);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    // Create A -> A (self-loop)
    Obj* a = mk_test_pair(NULL, NULL);
    a->a = a;

    SCC* result = compute_sccs(reg, a);

    if (!result) { FAIL("result should not be NULL"); goto cleanup; }
    if (result->member_count != 1) { FAIL("SCC should have 1 member"); goto cleanup; }

    free(a);
    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free(a);
    free_scc_registry(reg);
}

// Test cycle with tail: D -> A -> B -> C -> A
void test_scc_cycle_with_tail(void) {
    TEST(scc_cycle_with_tail);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    // Create A -> B -> C -> A cycle
    Obj* a = mk_test_pair(NULL, NULL);
    Obj* b = mk_test_pair(NULL, NULL);
    Obj* c = mk_test_pair(a, NULL);
    a->a = b;
    b->a = c;

    // D points to the cycle but is not part of it
    Obj* d = mk_test_pair(a, NULL);

    SCC* result = compute_sccs(reg, d);

    // Should have 2 SCCs: one with {A,B,C} and one with {D}
    int total_members = 0;
    int scc_count = 0;
    SCC* scc = result;
    while (scc) {
        total_members += scc->member_count;
        scc_count++;
        scc = scc->result_next;
    }
    if (total_members != 4) { FAIL("should have 4 total members"); goto cleanup; }
    if (scc_count != 2) { FAIL("should have 2 SCCs"); goto cleanup; }

    free(a); free(b); free(c); free(d);
    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free(a); free(b); free(c); free(d);
    free_scc_registry(reg);
}

// Test NULL root
void test_scc_null_root(void) {
    TEST(scc_null_root);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    SCC* result = compute_sccs(reg, NULL);
    if (result != NULL) { FAIL("NULL root should return NULL"); free_scc_registry(reg); return; }

    free_scc_registry(reg);
    PASS();
}

// Test is_frozen_after_construction
void test_is_frozen_after_construction(void) {
    TEST(is_frozen_after_construction);

    // Test with no mutations - should be frozen
    int result = is_frozen_after_construction("x", NULL);
    if (!result) { FAIL("NULL body should be frozen"); return; }

    PASS();
}

// Test detect_freeze_points (currently returns NULL)
void test_detect_freeze_points(void) {
    TEST(detect_freeze_points);

    FreezePoint* points = detect_freeze_points(NULL);
    if (points != NULL) { FAIL("stub should return NULL"); return; }

    PASS();
}

// Test tree with shared nodes (DAG, not cycle)
void test_scc_dag(void) {
    TEST(scc_dag);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    // Create DAG: A -> B, A -> C, B -> D, C -> D
    Obj* d = mk_test_int(4);
    Obj* b = mk_test_pair(d, NULL);
    Obj* c = mk_test_pair(d, NULL);
    Obj* a = mk_test_pair(b, c);

    SCC* result = compute_sccs(reg, a);

    // Each node should be in its own SCC (no cycles)
    int scc_count = 0;
    SCC* scc = result;
    while (scc) {
        if (scc->member_count != 1) { FAIL("each SCC should have 1 member"); goto cleanup; }
        scc_count++;
        scc = scc->result_next;
    }
    if (scc_count != 4) { FAIL("should have 4 SCCs"); goto cleanup; }

    free(a); free(b); free(c); free(d);
    free_scc_registry(reg);
    PASS();
    return;

cleanup:
    free(a); free(b); free(c); free(d);
    free_scc_registry(reg);
}

// Test release_scc with members
void test_release_scc_frees_members(void) {
    TEST(release_scc_frees_members);

    SCCRegistry* reg = mk_scc_registry();
    if (!reg) { FAIL("mk_scc_registry returned NULL"); return; }

    SCC* scc = create_scc(reg);
    Obj* obj = mk_test_int(42);
    add_to_scc(scc, obj);

    // Release once (ref_count goes to 0, members freed)
    release_scc(scc);

    if (scc->member_count != 0) { FAIL("members should be cleared"); free_scc_registry(reg); return; }
    if (scc->members != NULL) { FAIL("members ptr should be NULL"); free_scc_registry(reg); return; }

    free_scc_registry(reg);
    PASS();
}

int main(void) {
    printf("Running SCC Unit Tests...\n\n");

    test_scc_registry_lifecycle();
    test_create_scc();
    test_add_to_scc();
    test_find_scc();
    test_scc_refcount();
    test_scc_single_node();
    test_scc_linear_chain();
    test_scc_simple_cycle();
    test_scc_self_loop();
    test_scc_cycle_with_tail();
    test_scc_null_root();
    test_is_frozen_after_construction();
    test_detect_freeze_points();
    test_scc_dag();
    test_release_scc_frees_members();

    printf("\n========================================\n");
    printf("Results: %d passed, %d failed\n", tests_passed, tests_failed);
    printf("========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
