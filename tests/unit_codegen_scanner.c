// Unit test for field-aware scanner skipping weak fields
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include "../src/codegen/codegen.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", #name);
#define PASS() do { printf("PASS\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)

static char* capture_output(void (*fn)(void)) {
    FILE* tmp = tmpfile();
    if (!tmp) return NULL;

    int saved = dup(fileno(stdout));
    if (saved < 0) { fclose(tmp); return NULL; }

    fflush(stdout);
    if (dup2(fileno(tmp), fileno(stdout)) < 0) {
        close(saved);
        fclose(tmp);
        return NULL;
    }

    fn();
    fflush(stdout);

    dup2(saved, fileno(stdout));
    close(saved);

    fseek(tmp, 0, SEEK_END);
    long len = ftell(tmp);
    if (len < 0) { fclose(tmp); return NULL; }
    rewind(tmp);

    char* buf = malloc((size_t)len + 1);
    if (!buf) { fclose(tmp); return NULL; }
    size_t read_len = fread(buf, 1, (size_t)len, tmp);
    buf[read_len] = '\0';
    fclose(tmp);
    return buf;
}

static void gen_node_scanner(void) {
    gen_field_aware_scanner("Node");
}

static void test_weak_field_skipped(void) {
    TEST(weak_field_skipped);

    TYPE_REGISTRY = NULL;
    TypeField* fields = malloc(sizeof(TypeField));
    if (!fields) { FAIL("alloc fields"); return; }

    fields[0].name = "next";
    fields[0].type = "Node";
    fields[0].is_scannable = 1;
    fields[0].strength = FIELD_STRONG;

    register_type("Node", fields, 1);
    fields[0].strength = FIELD_WEAK;

    char* output = capture_output(gen_node_scanner);
    if (!output) { free(fields); FAIL("capture failed"); return; }

    if (!strstr(output, "void scan_Node")) {
        free(output);
        free(fields);
        FAIL("scanner not generated");
        return;
    }

    if (strstr(output, "scan_Node(x->next)")) {
        free(output);
        free(fields);
        FAIL("weak field should not be scanned");
        return;
    }

    free(output);
    free(fields);
    PASS();
}

int main(void) {
    test_weak_field_skipped();

    if (tests_failed) {
        fprintf(stderr, "%d tests failed\n", tests_failed);
        return 1;
    }
    printf("%d tests passed\n", tests_passed);
    return 0;
}
