// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "interpolation.h"
#include "gg/json_encode.h"
#include <config_reader.h>
#include <gg/attr.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/io.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <stddef.h>
#include <stdint.h>

NONNULL(1)
static void ggl_writer_call_chained(
    GgError *err, GgWriter writer, GgBuffer buf
) {
    if (*err == GG_ERR_OK) {
        *err = gg_writer_call(writer, buf);
    }
}

static GgError split_recipe_variable(
    GgBuffer recipe_variable, GgBuffer *namespace, GgBuffer *key
) {
    for (size_t i = 0; i < recipe_variable.len; i++) {
        if (recipe_variable.data[i] == ':') {
            *namespace = gg_buffer_substr(recipe_variable, 0, i);
            *key = gg_buffer_substr(recipe_variable, i + 1, SIZE_MAX);
            return GG_ERR_OK;
        }
    }

    GG_LOGE("No : found in recipe variable.");
    return GG_ERR_FAILURE;
}

static GgError write_object_as_buffer(void *ctx, GgObject object) {
    GgWriter *writer = ctx;
    if (gg_obj_type(object) == GG_TYPE_BUF) {
        return gg_writer_call(*writer, gg_obj_into_buf(object));
    }

    return gg_json_encode(object, *writer);
}

GgError ggl_substitute_escape(
    GgWriter writer,
    GgBuffer recipe_variable,
    GgBuffer root_path,
    GgBuffer component_name,
    GgBuffer component_version,
    GgBuffer thing_name,
    GgConfigReader config_reader
) {
    GgBuffer namespace;
    GgBuffer key;
    GgError ret = split_recipe_variable(recipe_variable, &namespace, &key);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGT(
        "Current variable substitution: %.*s. type = %.*s; arg = %.*s",
        (int) recipe_variable.len,
        recipe_variable.data,
        (int) namespace.len,
        namespace.data,
        (int) key.len,
        key.data
    );

    if (gg_buffer_eq(namespace, GG_STR("kernel"))) {
        if (gg_buffer_eq(key, GG_STR("rootPath"))) {
            return gg_writer_call(writer, root_path);
        }
    } else if (gg_buffer_eq(namespace, GG_STR("iot"))) {
        if (gg_buffer_eq(key, GG_STR("thingName"))) {
            return gg_writer_call(writer, thing_name);
        }
    } else if (gg_buffer_eq(namespace, GG_STR("work"))) {
        if (gg_buffer_eq(key, GG_STR("path"))) {
            ggl_writer_call_chained(&ret, writer, root_path);
            ggl_writer_call_chained(&ret, writer, GG_STR("/work/"));
            ggl_writer_call_chained(&ret, writer, component_name);
            ggl_writer_call_chained(&ret, writer, GG_STR("/"));
            return ret;
        }
    } else if (gg_buffer_eq(namespace, GG_STR("artifacts"))) {
        if (gg_buffer_eq(key, GG_STR("path"))) {
            ggl_writer_call_chained(&ret, writer, root_path);
            ggl_writer_call_chained(&ret, writer, GG_STR("/packages/"));
            ggl_writer_call_chained(&ret, writer, GG_STR("artifacts/"));
            ggl_writer_call_chained(&ret, writer, component_name);
            ggl_writer_call_chained(&ret, writer, GG_STR("/"));
            ggl_writer_call_chained(&ret, writer, component_version);
            ggl_writer_call_chained(&ret, writer, GG_STR("/"));
            return ret;
        }
        if (gg_buffer_eq(key, GG_STR("decompressedPath"))) {
            ggl_writer_call_chained(&ret, writer, root_path);
            ggl_writer_call_chained(&ret, writer, GG_STR("/packages/"));
            ggl_writer_call_chained(
                &ret, writer, GG_STR("artifacts-unarchived/")
            );
            ggl_writer_call_chained(&ret, writer, component_name);
            ggl_writer_call_chained(&ret, writer, GG_STR("/"));
            ggl_writer_call_chained(&ret, writer, component_version);
            ggl_writer_call_chained(&ret, writer, GG_STR("/"));
            return ret;
        }
    } else if (gg_buffer_eq(namespace, GG_STR("configuration"))) {
        GgObjectReceiver object_writer
            = { .ctx = &writer, .submit = write_object_as_buffer };
        return ggl_config_reader_call(config_reader, key, object_writer);
    }

    GG_LOGE(
        "Unhandled variable substitution: %.*s.",
        (int) recipe_variable.len,
        recipe_variable.data
    );
    return GG_ERR_FAILURE;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <gg/vector.h>
#include <unity.h>

#define TEST_ROOT_PATH GG_STR("/greengrass/v2")
#define TEST_COMPONENT GG_STR("com.example.MyComponent")
#define TEST_VERSION GG_STR("1.2.3")
#define TEST_THING_NAME GG_STR("MyThingName")

static GgError dummy_config_reader_call(
    void *ctx, GgBuffer key, GgObjectReceiver receiver
) {
    (void) ctx;
    (void) key;
    return gg_object_receiver_submit(
        receiver, gg_obj_buf(GG_STR("dummy_config_value"))
    );
}

static GgConfigReader dummy_config_reader(void) {
    return (GgConfigReader) { .reader = dummy_config_reader_call };
}

static GgBuffer run_substitute(GgBuffer escape_seq) {
    static uint8_t buf[512];
    GgByteVec vec = GG_BYTE_VEC(buf);
    GgError ret = ggl_substitute_escape(
        gg_byte_vec_writer(&vec),
        escape_seq,
        TEST_ROOT_PATH,
        TEST_COMPONENT,
        TEST_VERSION,
        TEST_THING_NAME,
        dummy_config_reader()
    );
    TEST_ASSERT_EQUAL_INT(GG_ERR_OK, ret);
    return vec.buf;
}

GG_TEST_DEFINE(substitute_kernel_root_path) {
    GgBuffer result = run_substitute(GG_STR("kernel:rootPath"));
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("/greengrass/v2"), result);
}

GG_TEST_DEFINE(substitute_iot_thing_name) {
    GgBuffer result = run_substitute(GG_STR("iot:thingName"));
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("MyThingName"), result);
}

GG_TEST_DEFINE(substitute_work_path) {
    GgBuffer result = run_substitute(GG_STR("work:path"));
    GG_TEST_ASSERT_BUF_EQUAL(
        GG_STR("/greengrass/v2/work/com.example.MyComponent/"), result
    );
}

GG_TEST_DEFINE(substitute_artifacts_path) {
    GgBuffer result = run_substitute(GG_STR("artifacts:path"));
    GG_TEST_ASSERT_BUF_EQUAL(
        GG_STR(
            "/greengrass/v2/packages/artifacts/com.example.MyComponent/1.2.3/"
        ),
        result
    );
}

GG_TEST_DEFINE(substitute_artifacts_decompressed_path) {
    GgBuffer result = run_substitute(GG_STR("artifacts:decompressedPath"));
    GG_TEST_ASSERT_BUF_EQUAL(
        GG_STR("/greengrass/v2/packages/artifacts-unarchived/"
               "com.example.MyComponent/1.2.3/"),
        result
    );
}

GG_TEST_DEFINE(substitute_configuration_uses_reader) {
    GgBuffer result = run_substitute(GG_STR("configuration:/some/key"));
    GG_TEST_ASSERT_BUF_EQUAL(GG_STR("dummy_config_value"), result);
}

GG_TEST_DEFINE(substitute_configuration_null_reader_fails) {
    static uint8_t buf[64];
    GgByteVec vec = GG_BYTE_VEC(buf);
    GgError ret = ggl_substitute_escape(
        gg_byte_vec_writer(&vec),
        GG_STR("configuration:/key"),
        TEST_ROOT_PATH,
        TEST_COMPONENT,
        TEST_VERSION,
        TEST_THING_NAME,
        GGL_CONFIG_NULL_READER
    );
    TEST_ASSERT_NOT_EQUAL(GG_ERR_OK, ret);
}

GG_TEST_DEFINE(substitute_no_colon_fails) {
    static uint8_t buf[64];
    GgByteVec vec = GG_BYTE_VEC(buf);
    GgError ret = ggl_substitute_escape(
        gg_byte_vec_writer(&vec),
        GG_STR("invalid"),
        TEST_ROOT_PATH,
        TEST_COMPONENT,
        TEST_VERSION,
        TEST_THING_NAME,
        dummy_config_reader()
    );
    TEST_ASSERT_NOT_EQUAL(GG_ERR_OK, ret);
}

GG_TEST_DEFINE(substitute_unknown_type_fails) {
    static uint8_t buf[64];
    GgByteVec vec = GG_BYTE_VEC(buf);
    GgError ret = ggl_substitute_escape(
        gg_byte_vec_writer(&vec),
        GG_STR("unknown:something"),
        TEST_ROOT_PATH,
        TEST_COMPONENT,
        TEST_VERSION,
        TEST_THING_NAME,
        dummy_config_reader()
    );
    TEST_ASSERT_NOT_EQUAL(GG_ERR_OK, ret);
}

GG_TEST_DEFINE(substitute_kernel_unknown_arg_fails) {
    static uint8_t buf[64];
    GgByteVec vec = GG_BYTE_VEC(buf);
    GgError ret = ggl_substitute_escape(
        gg_byte_vec_writer(&vec),
        GG_STR("kernel:unknown"),
        TEST_ROOT_PATH,
        TEST_COMPONENT,
        TEST_VERSION,
        TEST_THING_NAME,
        dummy_config_reader()
    );
    TEST_ASSERT_NOT_EQUAL(GG_ERR_OK, ret);
}

GG_TEST_DEFINE(substitute_iot_unknown_arg_fails) {
    static uint8_t buf[64];
    GgByteVec vec = GG_BYTE_VEC(buf);
    GgError ret = ggl_substitute_escape(
        gg_byte_vec_writer(&vec),
        GG_STR("iot:unknown"),
        TEST_ROOT_PATH,
        TEST_COMPONENT,
        TEST_VERSION,
        TEST_THING_NAME,
        dummy_config_reader()
    );
    TEST_ASSERT_NOT_EQUAL(GG_ERR_OK, ret);
}

#endif
