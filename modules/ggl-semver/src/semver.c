// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ctype.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/semver.h>
#include <limits.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

static bool process_version(
    GgByteVec current_requirement, char *current_version
) {
    bool return_status = false;

    if (current_requirement.buf.data[0] == '>') {
        if (current_requirement.buf.data[1] == '=') {
            if (strverscmp(
                    current_version, (char *) &current_requirement.buf.data[2]
                )
                >= 0) {
                return_status = true;
            }

        } else if (strverscmp(
                       current_version,
                       (char *) &current_requirement.buf.data[1]
                   )
                   > 0) {
            return_status = true;
        }

    } else if (current_requirement.buf.data[0] == '<') {
        if (current_requirement.buf.data[1] == '=') {
            if (strverscmp(
                    current_version, (char *) &current_requirement.buf.data[2]
                )
                <= 0) {
                return_status = true;
            }

        } else if (strverscmp(
                       current_version,
                       (char *) &current_requirement.buf.data[1]
                   )
                   < 0) {
            return_status = true;
        }
    } else if (((current_requirement.buf.data[0] == '=')
                && (strverscmp(
                        current_version,
                        (char *) &current_requirement.buf.data[1]
                    )
                    == 0))
               || ((isdigit(current_requirement.buf.data[0]))
                   && (strverscmp(
                           current_version,
                           (char *) &current_requirement.buf.data[0]
                       )
                       == 0))) {
        return_status = true;
    } else {
        return_status = false;
    }
    return return_status;
}

static bool is_digit_byte(uint8_t c) {
    return (c >= '0') && (c <= '9');
}

static bool is_identifier_byte(uint8_t c) {
    return is_digit_byte(c) || ((c >= 'A') && (c <= 'Z'))
        || ((c >= 'a') && (c <= 'z')) || (c == '-');
}

// Parse a numeric identifier (major/minor/patch). Advances *i. A numeric
// identifier is one or more digits with no leading zero unless it is "0".
static bool parse_numeric_identifier(GgBuffer version, size_t *i) {
    size_t start = *i;
    while ((*i < version.len) && is_digit_byte(version.data[*i])) {
        (*i)++;
    }
    size_t len = *i - start;
    if (len == 0) {
        return false;
    }
    if ((len > 1) && (version.data[start] == '0')) {
        return false;
    }
    return true;
}

// Parse dot-separated pre-release or build identifiers starting at *i until
// the end of the buffer or, for pre-release, the '+' that begins build
// metadata. Each identifier must be non-empty and contain only [0-9A-Za-z-].
// When numeric_no_leading_zero is set (pre-release), an all-digit identifier
// must not have a leading zero.
static bool parse_dot_separated_identifiers(
    GgBuffer version, size_t *i, bool prerelease
) {
    for (;;) {
        size_t start = *i;
        bool all_digits = true;
        while (*i < version.len) {
            uint8_t c = version.data[*i];
            if ((c == '.') || (prerelease && (c == '+'))) {
                break;
            }
            if (!is_identifier_byte(c)) {
                return false;
            }
            if (!is_digit_byte(c)) {
                all_digits = false;
            }
            (*i)++;
        }
        size_t len = *i - start;
        if (len == 0) {
            return false;
        }
        if (prerelease && all_digits && (len > 1)
            && (version.data[start] == '0')) {
            return false;
        }
        if ((*i < version.len) && (version.data[*i] == '.')) {
            (*i)++;
            continue;
        }
        return true;
    }
}

bool is_valid_semver(GgBuffer version) {
    size_t i = 0;

    // Version core: major.minor.patch
    if (!parse_numeric_identifier(version, &i)) {
        return false;
    }
    for (int part = 0; part < 2; part++) {
        if ((i >= version.len) || (version.data[i] != '.')) {
            return false;
        }
        i++;
        if (!parse_numeric_identifier(version, &i)) {
            return false;
        }
    }

    // Optional pre-release: '-' identifiers, terminated by end or '+'
    if ((i < version.len) && (version.data[i] == '-')) {
        i++;
        if (!parse_dot_separated_identifiers(version, &i, true)) {
            return false;
        }
    }

    // Optional build metadata: '+' identifiers, terminated by end
    if ((i < version.len) && (version.data[i] == '+')) {
        i++;
        if (!parse_dot_separated_identifiers(version, &i, false)) {
            return false;
        }
    }

    return i == version.len;
}

bool is_in_range(GgBuffer version, GgBuffer requirements_range) {
    char *requirements_range_as_char = (char *) requirements_range.data;

    static uint8_t work_mem_buffer[NAME_MAX];
    GgByteVec work_mem_vec = GG_BYTE_VEC(work_mem_buffer);

    static uint8_t current_version_buffer[NAME_MAX];
    GgByteVec current_version_vec = GG_BYTE_VEC(current_version_buffer);
    GgError ret = gg_byte_vec_append(&current_version_vec, version);
    gg_byte_vec_chain_append(&ret, &current_version_vec, GG_STR("\0"));
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to copy information over");
        return false;
    }

    for (ulong index = 0; index < requirements_range.len; index++) {
        if (requirements_range_as_char[index] == ' ') {
            // null terminating as strverscmp requires it
            ret = gg_byte_vec_append(&work_mem_vec, GG_STR("\0"));
            if (ret != GG_ERR_OK) {
                GG_LOGE("Failed to copy information over");
                return false;
            }
            bool result = process_version(
                work_mem_vec, (char *) current_version_vec.buf.data
            );
            if (result == false) {
                GG_LOGT("Requirement wasn't satisfied");
                return false;
            }
            // Rest once a value is parsed
            work_mem_vec.buf.len = 0;
            index++;
        }
        ret = gg_byte_vec_append(
            &work_mem_vec,
            (GgBuffer) { (uint8_t *) &requirements_range_as_char[index], 1 }
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to copy information over");
            return false;
        }
    }

    if (work_mem_vec.buf.len != 0) {
        ret = gg_byte_vec_append(&work_mem_vec, GG_STR("\0"));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to copy information over");
            return false;
        }
        bool result = process_version(
            work_mem_vec, (char *) current_version_vec.buf.data
        );
        if (result == false) {
            GG_LOGT("Requirement wasn't satisfied");
            return result;
        }
    }

    return true;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <unity.h>

GG_TEST_DEFINE(semver_exact_match) {
    TEST_ASSERT_TRUE(is_in_range(GG_STR("1.0.0"), GG_STR("1.0.0")));
}

GG_TEST_DEFINE(semver_exact_no_match) {
    TEST_ASSERT_FALSE(is_in_range(GG_STR("1.0.1"), GG_STR("1.0.0")));
}

GG_TEST_DEFINE(semver_gte) {
    TEST_ASSERT_TRUE(is_in_range(GG_STR("2.0.0"), GG_STR(">=1.0.0")));
    TEST_ASSERT_TRUE(is_in_range(GG_STR("1.0.0"), GG_STR(">=1.0.0")));
    TEST_ASSERT_FALSE(is_in_range(GG_STR("0.9.0"), GG_STR(">=1.0.0")));
}

GG_TEST_DEFINE(semver_lte) {
    TEST_ASSERT_TRUE(is_in_range(GG_STR("1.0.0"), GG_STR("<=2.0.0")));
    TEST_ASSERT_TRUE(is_in_range(GG_STR("2.0.0"), GG_STR("<=2.0.0")));
    TEST_ASSERT_FALSE(is_in_range(GG_STR("3.0.0"), GG_STR("<=2.0.0")));
}

GG_TEST_DEFINE(semver_gt) {
    TEST_ASSERT_TRUE(is_in_range(GG_STR("2.0.0"), GG_STR(">1.0.0")));
    TEST_ASSERT_FALSE(is_in_range(GG_STR("1.0.0"), GG_STR(">1.0.0")));
}

GG_TEST_DEFINE(semver_lt) {
    TEST_ASSERT_TRUE(is_in_range(GG_STR("0.9.0"), GG_STR("<1.0.0")));
    TEST_ASSERT_FALSE(is_in_range(GG_STR("1.0.0"), GG_STR("<1.0.0")));
}

GG_TEST_DEFINE(semver_range) {
    TEST_ASSERT_TRUE(is_in_range(GG_STR("1.5.0"), GG_STR(">=1.0.0 <=2.0.0")));
    TEST_ASSERT_FALSE(is_in_range(GG_STR("3.0.0"), GG_STR(">=1.0.0 <=2.0.0")));
}

GG_TEST_DEFINE(semver_valid_basic) {
    TEST_ASSERT_TRUE(is_valid_semver(GG_STR("0.0.0")));
    TEST_ASSERT_TRUE(is_valid_semver(GG_STR("1.2.3")));
    TEST_ASSERT_TRUE(is_valid_semver(GG_STR("10.20.30")));
    TEST_ASSERT_TRUE(is_valid_semver(GG_STR("1.2.3-alpha.1")));
    TEST_ASSERT_TRUE(is_valid_semver(GG_STR("1.2.3+build.5")));
    TEST_ASSERT_TRUE(is_valid_semver(GG_STR("1.2.3-rc.1+exp.sha.5114f85")));
}

GG_TEST_DEFINE(semver_valid_rejects_malformed) {
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2.3.4")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("01.2.3")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2.3-")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2.3-01")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2.x")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2.3 ")));
    TEST_ASSERT_FALSE(is_valid_semver(GG_STR("1.2.3\nfoo")));
}

#endif
