// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/object.h>
#include <string.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    union {
        bool boolean;
        int64_t i64;
        double f64;
        GglBuffer buf;
        GglList list;
        GglMap map;
    };

    uint8_t type;
} GglObjectPriv;

static_assert(
    sizeof(GglObject) == sizeof(GglObjectPriv), "GglObject impl invalid."
);
static_assert(
    alignof(GglObject) == alignof(GglObjectPriv), "GglObject impl invalid."
);

static GglObject obj_from_priv(GglObjectPriv obj) {
    GglObject result;
    memcpy(&result, &obj, sizeof(GglObject));
    return result;
}

static GglObjectPriv priv_from_obj(GglObject obj) {
    GglObjectPriv result;
    memcpy(&result, &obj, sizeof(GglObject));
    return result;
}

GglObjectType ggl_obj_type(GglObject obj) {
    return priv_from_obj(obj).type;
}

GglObject ggl_obj_bool(bool value) {
    return obj_from_priv((GglObjectPriv) { .boolean = value,
                                           .type = GGL_TYPE_BOOLEAN });
}

bool ggl_obj_into_bool(GglObject boolean) {
    assert(ggl_obj_type(boolean) == GGL_TYPE_BOOLEAN);
    return priv_from_obj(boolean).boolean;
}

GglObject ggl_obj_i64(int64_t value) {
    return obj_from_priv((GglObjectPriv) { .i64 = value, .type = GGL_TYPE_I64 }
    );
}

int64_t ggl_obj_into_i64(GglObject i64) {
    assert(ggl_obj_type(i64) == GGL_TYPE_I64);
    return priv_from_obj(i64).i64;
}

GglObject ggl_obj_f64(double value) {
    return obj_from_priv((GglObjectPriv) { .f64 = value, .type = GGL_TYPE_F64 }
    );
}

double ggl_obj_into_f64(GglObject f64) {
    assert(ggl_obj_type(f64) == GGL_TYPE_F64);
    return priv_from_obj(f64).f64;
}

GglObject ggl_obj_buf(GglBuffer value) {
    return obj_from_priv((GglObjectPriv) { .buf = value, .type = GGL_TYPE_BUF }
    );
}

GglBuffer ggl_obj_into_buf(GglObject buf) {
    assert(ggl_obj_type(buf) == GGL_TYPE_BUF);
    return priv_from_obj(buf).buf;
}

GglObject ggl_obj_map(GglMap value) {
    return obj_from_priv((GglObjectPriv) { .map = value, .type = GGL_TYPE_MAP }
    );
}

GglMap ggl_obj_into_map(GglObject map) {
    assert(ggl_obj_type(map) == GGL_TYPE_MAP);
    return priv_from_obj(map).map;
}

GglObject ggl_obj_list(GglList value) {
    return obj_from_priv((GglObjectPriv) { .list = value,
                                           .type = GGL_TYPE_LIST });
}

GglList ggl_obj_into_list(GglObject list) {
    assert(ggl_obj_type(list) == GGL_TYPE_LIST);
    return priv_from_obj(list).list;
}
