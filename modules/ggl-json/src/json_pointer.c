// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/json_pointer.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_gg_config_jsonp_parse(GgBuffer json_ptr, GgBufVec *key_path) {
    assert(key_path->capacity == GG_MAX_OBJECT_DEPTH);

    // TODO: Do full parsing of JSON pointer

    if ((json_ptr.len < 1) || (json_ptr.data[0] != '/')) {
        GG_LOGE("Invalid json pointer.");
        return GG_ERR_FAILURE;
    }

    size_t begin = 1;
    for (size_t i = 1; i < json_ptr.len; i++) {
        if (json_ptr.data[i] == '/') {
            GgError ret = gg_buf_vec_push(
                key_path, gg_buffer_substr(json_ptr, begin, i)
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE("Too many configuration levels.");
                return ret;
            }
            begin = i + 1;
        }
    }
    GgError ret = gg_buf_vec_push(
        key_path, gg_buffer_substr(json_ptr, begin, SIZE_MAX)
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Too many configuration levels.");
        return ret;
    }

    return GG_ERR_OK;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <unity.h>

GG_TEST_DEFINE(jsonp_parse_single_key) {
    GgBuffer keys[GG_MAX_OBJECT_DEPTH] = { 0 };
    GgBufVec key_path = GG_BUF_VEC(keys);
    GG_TEST_ASSERT_OK(ggl_gg_config_jsonp_parse(GG_STR("/foo"), &key_path));
    TEST_ASSERT_EQUAL(1, key_path.buf_list.len);
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("foo"), key_path.buf_list.bufs[0]);
}

GG_TEST_DEFINE(jsonp_parse_multiple_keys) {
    GgBuffer keys[GG_MAX_OBJECT_DEPTH] = { 0 };
    GgBufVec key_path = GG_BUF_VEC(keys);
    GG_TEST_ASSERT_OK(
        ggl_gg_config_jsonp_parse(GG_STR("/foo/bar/baz"), &key_path)
    );
    TEST_ASSERT_EQUAL(3, key_path.buf_list.len);
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("foo"), key_path.buf_list.bufs[0]);
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("bar"), key_path.buf_list.bufs[1]);
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("baz"), key_path.buf_list.bufs[2]);
}

GG_TEST_DEFINE(jsonp_parse_invalid_no_leading_slash) {
    GgBuffer keys[GG_MAX_OBJECT_DEPTH] = { 0 };
    GgBufVec key_path = GG_BUF_VEC(keys);
    GG_TEST_ASSERT_BAD(ggl_gg_config_jsonp_parse(GG_STR("foo"), &key_path));
}

GG_TEST_DEFINE(jsonp_parse_empty_string) {
    GgBuffer keys[GG_MAX_OBJECT_DEPTH] = { 0 };
    GgBufVec key_path = GG_BUF_VEC(keys);
    GG_TEST_ASSERT_BAD(ggl_gg_config_jsonp_parse(GG_STR(""), &key_path));
}

#endif
