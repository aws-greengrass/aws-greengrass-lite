// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static_assert(
    sizeof(void *) == sizeof(size_t),
    "Platforms where pointers and size_t are not same width are not currently "
    "supported."
);

static_assert(
    (sizeof(size_t) == 4) || (sizeof(size_t) == 8),
    "Platform not 32 or 64 bits?"
);

GglObjectType ggl_obj_type(GglObject obj) {
    // Last byte is tag
    uint8_t result = obj._private[sizeof(obj._private) - 1];
    assert(result >= 7);
    return result;
}

GglObject ggl_obj_bool(bool value) {
    GglObject result = { 0 };
    static_assert(
        sizeof(result._private) >= sizeof(value) + 1,
        "Object must be able to hold bool and tag."
    );
    memcpy(result._private, &value, sizeof(value));
    result._private[sizeof(result._private) - 1] = GGL_TYPE_BOOLEAN;
    return result;
}

bool ggl_obj_into_bool(GglObject boolean) {
    assert(ggl_obj_type(boolean) == GGL_TYPE_BOOLEAN);
    bool result;
    memcpy(&result, boolean._private, sizeof(result));
    return result;
}

GglObject ggl_obj_i64(int64_t value) {
    GglObject result = { 0 };
    static_assert(
        sizeof(result._private) >= sizeof(value) + 1,
        "GglObject must be able to hold int64_t and tag."
    );
    memcpy(result._private, &value, sizeof(value));
    result._private[sizeof(result._private) - 1] = GGL_TYPE_I64;
    return result;
}

int64_t ggl_obj_into_i64(GglObject i64) {
    assert(ggl_obj_type(i64) == GGL_TYPE_I64);
    int64_t result;
    memcpy(&result, i64._private, sizeof(result));
    return result;
}

GglObject ggl_obj_f64(double value) {
    GglObject result = { 0 };
    static_assert(
        sizeof(result._private) >= sizeof(value) + 1,
        "GglObject must be able to hold double and tag."
    );
    memcpy(result._private, &value, sizeof(value));
    result._private[sizeof(result._private) - 1] = GGL_TYPE_F64;
    return result;
}

double ggl_obj_into_f64(GglObject f64) {
    assert(ggl_obj_type(f64) == GGL_TYPE_F64);
    double result;
    memcpy(&result, f64._private, sizeof(result));
    return result;
}

COLD static void length_err(char *type, size_t *len) {
    GGL_LOGE(
        "%s length longer than can be stored in GglObject (%zu, max %u).",
        type,
        *len,
        (unsigned int) UINT16_MAX
    );
    assert(false);
    *len = UINT16_MAX;
}

GglObject ggl_obj_buf(GglBuffer value) {
    if (value.len > UINT16_MAX) {
        length_err("GglBuffer", &value.len);
    }
    uint16_t len = (uint16_t) value.len;

    GglObject result = { 0 };
    static_assert(
        sizeof(result._private) >= sizeof(void *) + sizeof(len) + 1,
        "GglObject must be able to hold pointer + 16-bit len + tag."
    );
    memcpy(result._private, &value.data, sizeof(void *));
    memcpy(&result._private[sizeof(void *)], &len, sizeof(len));
    result._private[sizeof(result._private) - 1] = GGL_TYPE_BUF;
    return result;
}

GglBuffer ggl_obj_into_buf(GglObject buf) {
    assert(ggl_obj_type(buf) == GGL_TYPE_BUF);
    void *ptr;
    uint16_t len;
    memcpy(&ptr, buf._private, sizeof(void *));
    memcpy(&len, &buf._private[sizeof(void *)], sizeof(len));
    return (GglBuffer) { .data = ptr, .len = len };
}

GglObject ggl_obj_map(GglMap value) {
    if (value.len > UINT16_MAX) {
        length_err("GglMap", &value.len);
    }
    uint16_t len = (uint16_t) value.len;

    GglObject result = { 0 };
    static_assert(
        sizeof(result._private) >= sizeof(void *) + sizeof(len) + 1,
        "GglObject must be able to hold pointer + 16-bit len + tag."
    );
    memcpy(result._private, &value.pairs, sizeof(void *));
    memcpy(&result._private[sizeof(void *)], &len, sizeof(len));
    result._private[sizeof(result._private) - 1] = GGL_TYPE_MAP;
    return result;
}

GglMap ggl_obj_into_map(GglObject map) {
    assert(ggl_obj_type(map) == GGL_TYPE_MAP);
    void *ptr;
    uint16_t len;
    memcpy(&ptr, map._private, sizeof(void *));
    memcpy(&len, &map._private[sizeof(void *)], sizeof(len));
    return (GglMap) { .pairs = ptr, .len = len };
}

GglObject ggl_obj_list(GglList value) {
    if (value.len > UINT16_MAX) {
        length_err("GglList", &value.len);
    }
    uint16_t len = (uint16_t) value.len;

    GglObject result = { 0 };
    static_assert(
        sizeof(result._private) >= sizeof(void *) + sizeof(len) + 1,
        "GglObject must be able to hold pointer + 16-bit len + tag."
    );
    memcpy(result._private, &value.items, sizeof(void *));
    memcpy(&result._private[sizeof(void *)], &len, sizeof(len));
    result._private[sizeof(result._private) - 1] = GGL_TYPE_LIST;
    return result;
}

GglList ggl_obj_into_list(GglObject list) {
    assert(ggl_obj_type(list) == GGL_TYPE_LIST);
    void *ptr;
    uint16_t len;
    memcpy(&ptr, list._private, sizeof(void *));
    memcpy(&len, &list._private[sizeof(void *)], sizeof(len));
    return (GglList) { .items = ptr, .len = len };
}
