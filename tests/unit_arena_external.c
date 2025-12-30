#include <stdio.h>
#include <stdlib.h>
#include "src/memory/arena.h"

typedef struct MockObj {
    int ref_count;
} MockObj;

static int release_count = 0;

static void mock_release(void* ptr) {
    MockObj* obj = (MockObj*)ptr;
    if (!obj) return;
    obj->ref_count--;
    release_count++;
    if (obj->ref_count == 0) {
        // Defer free so the test can safely validate release_count.
    }
}

int main(void) {
    Arena* a = arena_create(1024);
    MockObj* obj = malloc(sizeof(MockObj));
    if (!obj) {
        fprintf(stderr, "alloc failed\n");
        return 1;
    }
    obj->ref_count = 1;

    arena_register_external(a, obj, mock_release);
    arena_destroy(a);

    if (release_count != 1) {
        fprintf(stderr, "expected 1 release, got %d\n", release_count);
        free(obj);
        return 1;
    }

    free(obj);
    return 0;
}
