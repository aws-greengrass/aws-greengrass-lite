// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/policy_validation.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_validate_policy_resource(GgBuffer resource) {
    for (size_t i = 0; i < resource.len; i++) {
        uint8_t c = resource.data[i];
        if (c == (uint8_t) '?') {
            GG_LOGE(
                "Invalid policy resource: bare '?' at position %d in '%.*s'.",
                (int) i,
                (int) resource.len,
                resource.data
            );
            return GG_ERR_INVALID;
        }
        if ((c == (uint8_t) '$') && (i + 1 < resource.len)
            && (resource.data[i + 1] == (uint8_t) '{')) {
            if (i + 3 >= resource.len) {
                GG_LOGE(
                    "Invalid policy resource: unterminated escape in '%.*s'.",
                    (int) resource.len,
                    resource.data
                );
                return GG_ERR_INVALID;
            }
            uint8_t escaped = resource.data[i + 2];
            if (((escaped != (uint8_t) '*') && (escaped != (uint8_t) '?')
                 && (escaped != (uint8_t) '$'))
                || (resource.data[i + 3] != (uint8_t) '}')) {
                GG_LOGE(
                    "Invalid policy resource: bad escape sequence in '%.*s'.",
                    (int) resource.len,
                    resource.data
                );
                return GG_ERR_INVALID;
            }
            i += 3;
        }
    }
    return GG_ERR_OK;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <unity.h>

GG_TEST_DEFINE(policy_resource_simple_topic) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("test/topic")));
}

GG_TEST_DEFINE(policy_resource_wildcard_star) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("*")));
}

GG_TEST_DEFINE(policy_resource_embedded_star) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("factory/1/*/status"))
    );
}

GG_TEST_DEFINE(policy_resource_dollar_no_brace) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("$aws/things/x")));
}

GG_TEST_DEFINE(policy_resource_escape_star) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("${*}")));
}

GG_TEST_DEFINE(policy_resource_escape_question) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("${?}")));
}

GG_TEST_DEFINE(policy_resource_escape_dollar) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("${$}")));
}

GG_TEST_DEFINE(policy_resource_literals_hash_plus) {
    GG_TEST_ASSERT_OK(ggl_validate_policy_resource(GG_STR("a/b#c+d")));
}

GG_TEST_DEFINE(policy_resource_bare_question) {
    TEST_ASSERT_EQUAL_INT(
        GG_ERR_INVALID,
        ggl_validate_policy_resource(GG_STR("invalid?/resource"))
    );
}

GG_TEST_DEFINE(policy_resource_bad_escape_char) {
    TEST_ASSERT_EQUAL_INT(
        GG_ERR_INVALID, ggl_validate_policy_resource(GG_STR("a${x}b"))
    );
}

GG_TEST_DEFINE(policy_resource_unterminated_dollar_brace) {
    TEST_ASSERT_EQUAL_INT(
        GG_ERR_INVALID, ggl_validate_policy_resource(GG_STR("a${"))
    );
}

GG_TEST_DEFINE(policy_resource_unterminated_escape_no_close) {
    TEST_ASSERT_EQUAL_INT(
        GG_ERR_INVALID, ggl_validate_policy_resource(GG_STR("a${?"))
    );
}

#endif
