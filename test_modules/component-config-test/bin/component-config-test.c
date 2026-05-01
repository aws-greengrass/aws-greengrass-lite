// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// Unit tests for apply_component_to_configuration() in component_config.c.
//
// apply_component_to_configuration() looks up a component in the
// component_to_configuration map, then extracts merge/reset payloads from
// the canonical internal form (a map with optional "merge" and "reset"
// keys). These tests exercise parsing and validation of the canonical form.
//
// Tests that provide valid merge/reset data will attempt core-bus writes
// which fail without a running ggconfigd — we verify the function does NOT
// return GG_ERR_INVALID (meaning parsing succeeded).
//
// Exercises canonical form scenarios:
//   1. Canonical map with "merge" key (valid, write attempted)
//   2. Canonical map with "merge" key — different structure
//   3. Canonical map with only "reset" key (no merge -> reset attempted)
//   4. Per-component value is GG_TYPE_LIST (invalid -> GG_ERR_INVALID)
//   5. Per-component value is GG_TYPE_BUF (invalid -> GG_ERR_INVALID)
//   6. Empty canonical map (no merge, no reset -> GG_ERR_OK, no-op)
//   7. Canonical map with both "merge" and "reset" keys

#include "component_config.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
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
// Test 1: Canonical form with "merge" map (write attempted)
// ---------------------------------------------------------------------------
static void test_canonical_merge(void) {
    const char *name = "canonical_merge";
    TEST(name);

    // Build canonical form: {"merge": {"networkProxy": {"proxy": {"url":
    // "..."}}}}
    GgObject url = gg_obj_buf(GG_STR("http://127.0.0.1:3129"));
    GgMap proxy_map = GG_MAP(gg_kv(GG_STR("url"), url));
    GgObject proxy_obj = gg_obj_map(proxy_map);
    GgMap network_proxy_map = GG_MAP(gg_kv(GG_STR("proxy"), proxy_obj));
    GgObject network_proxy_obj = gg_obj_map(network_proxy_map);
    GgMap merge_map = GG_MAP(gg_kv(GG_STR("networkProxy"), network_proxy_obj));
    GgObject merge_obj = gg_obj_map(merge_map);

    GgMap canonical = GG_MAP(gg_kv(GG_STR("merge"), merge_obj));
    GgObject canonical_obj = gg_obj_map(canonical);

    GgMap component_to_config
        = GG_MAP(gg_kv(GG_STR("myComponent"), canonical_obj));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    // Parsing succeeds; write to ggconfigd will fail (no server) but
    // should NOT be GG_ERR_INVALID.
    if (ret == GG_ERR_INVALID) {
        TEST_FAIL(name, "got GG_ERR_INVALID — parsing should have succeeded");
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 2: Canonical form with "merge" map — different structure
// ---------------------------------------------------------------------------
static void test_canonical_merge_simple(void) {
    const char *name = "canonical_merge_simple";
    TEST(name);

    GgObject port_val = gg_obj_buf(GG_STR("3128"));
    GgMap merge_map = GG_MAP(gg_kv(GG_STR("port"), port_val));
    GgObject merge_obj = gg_obj_map(merge_map);

    GgMap canonical = GG_MAP(gg_kv(GG_STR("merge"), merge_obj));
    GgObject canonical_obj = gg_obj_map(canonical);

    GgMap component_to_config
        = GG_MAP(gg_kv(GG_STR("myComponent"), canonical_obj));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    if (ret == GG_ERR_INVALID) {
        TEST_FAIL(name, "got GG_ERR_INVALID — parsing should have succeeded");
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 3: Canonical form with only "reset" (no merge -> reset attempted)
// ---------------------------------------------------------------------------
static void test_canonical_reset_only(void) {
    const char *name = "canonical_reset_only";
    TEST(name);

    GgObject reset_path = gg_obj_buf(GG_STR("/networkProxy"));
    GgList reset_list = GG_LIST(reset_path);
    GgObject reset_obj = gg_obj_list(reset_list);

    GgMap canonical = GG_MAP(gg_kv(GG_STR("reset"), reset_obj));
    GgObject canonical_obj = gg_obj_map(canonical);

    GgMap component_to_config
        = GG_MAP(gg_kv(GG_STR("myComponent"), canonical_obj));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    // Parsing succeeds; delete will fail (no server) but should NOT be
    // GG_ERR_INVALID.
    if (ret == GG_ERR_INVALID) {
        TEST_FAIL(name, "got GG_ERR_INVALID — parsing should have succeeded");
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 4: Per-component value is GG_TYPE_LIST (invalid -> GG_ERR_INVALID)
// ---------------------------------------------------------------------------
static void test_invalid_type_list(void) {
    const char *name = "invalid_type_list";
    TEST(name);

    GgObject list_val
        = gg_obj_list(GG_LIST(gg_obj_buf(GG_STR("a")), gg_obj_buf(GG_STR("b")))
        );

    GgMap component_to_config = GG_MAP(gg_kv(GG_STR("myComponent"), list_val));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    if (ret != GG_ERR_INVALID) {
        TEST_FAIL(
            name, "expected GG_ERR_INVALID for LIST input, got %d", (int) ret
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 5: Per-component value is GG_TYPE_BUF (invalid in canonical form)
// ---------------------------------------------------------------------------
static void test_invalid_type_buf(void) {
    const char *name = "invalid_type_buf";
    TEST(name);

    GgObject buf_val = gg_obj_buf(GG_STR("{\"merge\":{}}"));

    GgMap component_to_config = GG_MAP(gg_kv(GG_STR("myComponent"), buf_val));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    if (ret != GG_ERR_INVALID) {
        TEST_FAIL(
            name, "expected GG_ERR_INVALID for BUF input, got %d", (int) ret
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 6: Empty canonical map (no merge, no reset -> GG_ERR_OK)
// ---------------------------------------------------------------------------
static void test_empty_canonical_map(void) {
    const char *name = "empty_canonical_map";
    TEST(name);

    // An empty map: no "merge", no "reset" — nothing to do.
    GgKV empty_kv = gg_kv(GG_STR(""), GG_OBJ_NULL);
    GgMap empty_map = { .pairs = &empty_kv, .len = 0 };
    GgObject canonical_obj = gg_obj_map(empty_map);

    GgMap component_to_config
        = GG_MAP(gg_kv(GG_STR("myComponent"), canonical_obj));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    if (ret != GG_ERR_OK) {
        TEST_FAIL(
            name,
            "expected GG_ERR_OK for empty canonical map, got %d",
            (int) ret
        );
        return;
    }
    TEST_PASS(name);
}

// ---------------------------------------------------------------------------
// Test 7: Canonical form with both "merge" and "reset" keys
// ---------------------------------------------------------------------------
static void test_canonical_merge_and_reset(void) {
    const char *name = "canonical_merge_and_reset";
    TEST(name);

    GgObject port_val = gg_obj_buf(GG_STR("3128"));
    GgMap merge_map = GG_MAP(gg_kv(GG_STR("port"), port_val));
    GgObject merge_obj = gg_obj_map(merge_map);

    GgObject reset_path = gg_obj_buf(GG_STR("/legacyKey"));
    GgList reset_list = GG_LIST(reset_path);
    GgObject reset_obj = gg_obj_list(reset_list);

    GgMap canonical = GG_MAP(
        gg_kv(GG_STR("merge"), merge_obj), gg_kv(GG_STR("reset"), reset_obj)
    );
    GgObject canonical_obj = gg_obj_map(canonical);

    GgMap component_to_config
        = GG_MAP(gg_kv(GG_STR("myComponent"), canonical_obj));

    GgError ret = apply_component_to_configuration(
        GG_STR("myComponent"), component_to_config
    );

    // Parsing succeeds; core-bus calls will fail but should NOT be
    // GG_ERR_INVALID.
    if (ret == GG_ERR_INVALID) {
        TEST_FAIL(name, "got GG_ERR_INVALID — parsing should have succeeded");
        return;
    }
    TEST_PASS(name);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    test_canonical_merge();
    test_canonical_merge_simple();
    test_canonical_reset_only();
    test_canonical_merge_and_reset();
    test_invalid_type_list();
    test_invalid_type_buf();
    test_empty_canonical_map();

    fprintf(
        stderr,
        "\n===========\n%d test(s) run, %d failed\n===========\n",
        tests_run,
        tests_failed
    );
    return tests_failed == 0 ? 0 : 1;
}
