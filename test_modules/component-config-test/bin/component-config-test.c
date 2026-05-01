// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Unit tests for extract_merge_and_reset_payloads() in component_config.c.
//
// extract_merge_and_reset_payloads() is a pure function (no IPC, no globals)
// that parses a single componentToConfiguration entry and returns pointers
// to the merge and/or reset payloads, or NULL for no-op, or a GgError for
// bad input. These tests only exercise the merge extraction path — they
// pass NULL for reset_out, which instructs the helper to skip reset
// extraction entirely.
//
// Exercises all three accepted value shapes plus edge cases:
//   1. GG_TYPE_MAP direct form (Rust SDK / core-bus path)
//   2. GG_TYPE_BUF JSON with {"merge": {...}} (ggl-cli path)
//   3. GG_TYPE_BUF JSON with only {"reset": [...]} (no merge -> no-op)
//   4. GG_TYPE_LIST (invalid -> GG_ERR_INVALID)
//   5. GG_TYPE_BUF with malformed JSON (parse error propagated)

#include "component_config.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <stdint.h>
#include <stdio.h>

static int tests_run = 0;
static int tests_failed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        fprintf(stderr, "[ RUN  ] %s\n", name); \
    } while (0)

#define TEST_PASS(name) \
    do { \
        fprintf(stderr, "[  OK  ] %s\n", name); \
    } while (0)

#define TEST_FAIL(name, fmt, ...) \
    do { \
        tests_failed++; \
        fprintf(stderr, "[ FAIL ] %s: " fmt "\n", name, ##__VA_ARGS__); \
    } while (0)

// ---------------------------------------------------------------------------
// Test 1: GG_TYPE_MAP direct form (Rust SDK / core-bus)
// ---------------------------------------------------------------------------
static void test_map_direct_form(void) {
    const char *name = "map_direct_form";
    TEST(name);

    // Input: a direct merge map { "networkProxy": { "proxy": { "url": "..." }}}
    GgObject url = gg_obj_buf(GG_STR("http://127.0.0.1:3129"));
    GgMap proxy_map = GG_MAP(gg_kv(GG_STR("url"), url));
    GgObject proxy_obj = gg_obj_map(proxy_map);
    GgMap network_proxy_map = GG_MAP(gg_kv(GG_STR("proxy"), proxy_obj));
    GgObject network_proxy_obj = gg_obj_map(network_proxy_map);
    GgMap direct_merge
        = GG_MAP(gg_kv(GG_STR("networkProxy"), network_proxy_obj));
    GgObject config_update = gg_obj_map(direct_merge);

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = NULL;
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret != GG_ERR_OK) {
        TEST_FAIL(name, "expected GG_ERR_OK, got %d", (int) ret);
        return;
    }
    if (merge_out == NULL) {
        TEST_FAIL(name, "expected non-NULL merge_out");
        return;
    }
    if (gg_obj_type(*merge_out) != GG_TYPE_MAP) {
        TEST_FAIL(
            name,
            "expected merge_out to be GG_TYPE_MAP, got %d",
            (int) gg_obj_type(*merge_out)
        );
        return;
    }
    // For MAP form, merge_out should point right at our input.
    if (merge_out != &config_update) {
        TEST_FAIL(
            name, "expected merge_out to point at input config_update_obj"
        );
        return;
    }
    // Verify the map's keys are intact.
    GgMap m = gg_obj_into_map(*merge_out);
    if (m.len != 1) {
        TEST_FAIL(name, "expected map len 1, got %zu", m.len);
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 2: GG_TYPE_BUF JSON with "merge" wrapper (ggl-cli path)
// ---------------------------------------------------------------------------
static void test_buf_json_with_merge(void) {
    const char *name = "buf_json_with_merge";
    TEST(name);

    // Using a static buffer because gg_json_decode_destructive writes into it
    static char json[] = "{\"merge\":{\"networkProxy\":{\"proxy\":{\"url\":"
                         "\"http://127.0.0.1:3129\"}}}}";
    GgBuffer json_buf = { .data = (uint8_t *) json, .len = sizeof(json) - 1 };
    GgObject config_update = gg_obj_buf(json_buf);

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = NULL;
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret != GG_ERR_OK) {
        TEST_FAIL(name, "expected GG_ERR_OK, got %d", (int) ret);
        return;
    }
    if (merge_out == NULL) {
        TEST_FAIL(name, "expected non-NULL merge_out (merge key present)");
        return;
    }
    if (gg_obj_type(*merge_out) != GG_TYPE_MAP) {
        TEST_FAIL(
            name,
            "expected merge_out to be GG_TYPE_MAP, got %d",
            (int) gg_obj_type(*merge_out)
        );
        return;
    }
    // Check the parsed tree contains networkProxy.
    GgMap m = gg_obj_into_map(*merge_out);
    GgObject *np_obj = NULL;
    if (!gg_map_get(m, GG_STR("networkProxy"), &np_obj)) {
        TEST_FAIL(name, "parsed merge map missing 'networkProxy' key");
        return;
    }
    if (gg_obj_type(*np_obj) != GG_TYPE_MAP) {
        TEST_FAIL(name, "networkProxy is not a map");
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 3: GG_TYPE_BUF JSON without "merge" key -> no-op (GG_ERR_OK, NULL out)
// ---------------------------------------------------------------------------
static void test_buf_json_without_merge(void) {
    const char *name = "buf_json_without_merge";
    TEST(name);

    static char json[] = "{\"reset\":[\"/networkProxy\"]}";
    GgBuffer json_buf = { .data = (uint8_t *) json, .len = sizeof(json) - 1 };
    GgObject config_update = gg_obj_buf(json_buf);

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = (GgObject *) 0xDEADBEEF; // sentinel
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret != GG_ERR_OK) {
        TEST_FAIL(
            name,
            "expected GG_ERR_OK for missing 'merge' key, got %d",
            (int) ret
        );
        return;
    }
    if (merge_out != NULL) {
        TEST_FAIL(
            name,
            "expected merge_out == NULL for JSON without 'merge' key, got %p",
            (void *) merge_out
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 4: GG_TYPE_LIST (invalid) -> GG_ERR_INVALID
// ---------------------------------------------------------------------------
static void test_invalid_type_list(void) {
    const char *name = "invalid_type_list";
    TEST(name);

    GgObject config_update
        = gg_obj_list(GG_LIST(gg_obj_buf(GG_STR("a")), gg_obj_buf(GG_STR("b")))
        );

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = (GgObject *) 0xDEADBEEF; // sentinel
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret != GG_ERR_INVALID) {
        TEST_FAIL(
            name, "expected GG_ERR_INVALID for LIST input, got %d", (int) ret
        );
        return;
    }
    // merge_out should have been zeroed by the helper up front
    if (merge_out != NULL) {
        TEST_FAIL(
            name,
            "expected merge_out == NULL on error, got %p",
            (void *) merge_out
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 5: GG_TYPE_BUF with malformed JSON -> parse error propagated
// ---------------------------------------------------------------------------
static void test_buf_malformed_json(void) {
    const char *name = "buf_malformed_json";
    TEST(name);

    static char json[] = "{this is not valid json";
    GgBuffer json_buf = { .data = (uint8_t *) json, .len = sizeof(json) - 1 };
    GgObject config_update = gg_obj_buf(json_buf);

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = (GgObject *) 0xDEADBEEF;
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret == GG_ERR_OK) {
        TEST_FAIL(name, "expected error for malformed JSON, got GG_ERR_OK");
        return;
    }
    if (merge_out != NULL) {
        TEST_FAIL(
            name,
            "expected merge_out == NULL on error, got %p",
            (void *) merge_out
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Bonus test 6: GG_TYPE_BUF with JSON that isn't an object (e.g. array)
// ---------------------------------------------------------------------------
static void test_buf_json_not_object(void) {
    const char *name = "buf_json_not_object";
    TEST(name);

    static char json[] = "[1, 2, 3]";
    GgBuffer json_buf = { .data = (uint8_t *) json, .len = sizeof(json) - 1 };
    GgObject config_update = gg_obj_buf(json_buf);

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = (GgObject *) 0xDEADBEEF;
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret != GG_ERR_INVALID) {
        TEST_FAIL(
            name,
            "expected GG_ERR_INVALID for JSON array input, got %d",
            (int) ret
        );
        return;
    }
    if (merge_out != NULL) {
        TEST_FAIL(
            name,
            "expected merge_out == NULL on error, got %p",
            (void *) merge_out
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 7: GG_TYPE_BUF JSON with BOTH "merge" and "reset" keys
// -> extract_merge_and_reset_payloads returns the merge object; reset is
//    ignored here because we pass NULL for reset_out (reset coverage is
//    exercised in the deployment integration tests).
// ---------------------------------------------------------------------------
static void test_buf_json_with_merge_and_reset(void) {
    const char *name = "buf_json_with_merge_and_reset";
    TEST(name);

    static char json[]
        = "{\"merge\":{\"port\":3128},\"reset\":[\"/legacyKey\"]}";
    GgBuffer json_buf = { .data = (uint8_t *) json, .len = sizeof(json) - 1 };
    GgObject config_update = gg_obj_buf(json_buf);

    static uint8_t arena_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(arena_mem));

    GgObject *merge_out = (GgObject *) 0xDEADBEEF;
    GgError ret = extract_merge_and_reset_payloads(
        &config_update, &alloc, &merge_out, NULL
    );

    if (ret != GG_ERR_OK) {
        TEST_FAIL(name, "expected GG_ERR_OK, got %d", (int) ret);
        return;
    }
    if (merge_out == NULL) {
        TEST_FAIL(name, "expected non-NULL merge_out when 'merge' is present");
        return;
    }
    if (gg_obj_type(*merge_out) != GG_TYPE_MAP) {
        TEST_FAIL(
            name,
            "expected merge_out to be GG_TYPE_MAP, got %d",
            (int) gg_obj_type(*merge_out)
        );
        return;
    }
    // The merge map should contain our "port" key.
    GgMap m = gg_obj_into_map(*merge_out);
    GgObject *port_obj = NULL;
    if (!gg_map_get(m, GG_STR("port"), &port_obj)) {
        TEST_FAIL(name, "parsed merge map missing 'port' key");
        return;
    }
    TEST_PASS(name);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    test_map_direct_form();
    test_buf_json_with_merge();
    test_buf_json_without_merge();
    test_buf_json_with_merge_and_reset();
    test_invalid_type_list();
    test_buf_malformed_json();
    test_buf_json_not_object();

    fprintf(
        stderr,
        "\n===========\n%d test(s) run, %d failed\n===========\n",
        tests_run,
        tests_failed
    );
    return tests_failed == 0 ? 0 : 1;
}
