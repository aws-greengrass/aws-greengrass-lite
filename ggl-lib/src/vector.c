// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/vector.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <string.h>

GglError ggl_obj_vec_push(GglObjVec *vector, GglObject object) {
    if (vector->list.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("ggl_obj_vec", "Pushed to %p.", vector);
    vector->list.items[vector->list.len] = object;
    vector->list.len++;
    return GGL_ERR_OK;
}

GglError ggl_obj_vec_pop(GglObjVec *vector, GglObject *out) {
    if (vector->list.len == 0) {
        return GGL_ERR_RANGE;
    }
    if (out != NULL) {
        *out = vector->list.items[vector->list.len - 1];
    }
    GGL_LOGT("ggl_obj_vec", "Popped from %p.", vector);

    vector->list.len--;
    return GGL_ERR_OK;
}

GglError ggl_kv_vec_push(GglKVVec *vector, GglKV kv) {
    if (vector->map.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("ggl_kv_vec", "Pushed to %p.", vector);
    vector->map.pairs[vector->map.len] = kv;
    vector->map.len++;
    return GGL_ERR_OK;
}

GglByteVec ggl_byte_vec_init(GglBuffer buf) {
    return (GglByteVec) { .buf = { .data = buf.data, .len = 0 },
                          .capacity = buf.len };
}

GglError ggl_byte_vec_push(GglByteVec *vector, uint8_t byte) {
    if (vector->buf.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("ggl_byte_vec", "Pushed to %p.", vector);
    vector->buf.data[vector->buf.len] = byte;
    vector->buf.len++;
    return GGL_ERR_OK;
}

void ggl_byte_vec_chain_push(GglError *err, GglByteVec *vector, uint8_t byte) {
    if (*err == GGL_ERR_OK) {
        *err = ggl_byte_vec_push(vector, byte);
    }
}

GglError ggl_byte_vec_append(GglByteVec *vector, GglBuffer buf) {
    if (vector->capacity - vector->buf.len < buf.len) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("ggl_byte_vec", "Appended to %p.", vector);
    memcpy(&vector->buf.data[vector->buf.len], buf.data, buf.len);
    vector->buf.len += buf.len;
    return GGL_ERR_OK;
}

void ggl_byte_vec_chain_append(
    GglError *err, GglByteVec *vector, GglBuffer buf
) {
    if (*err == GGL_ERR_OK) {
        *err = ggl_byte_vec_append(vector, buf);
    }
}
