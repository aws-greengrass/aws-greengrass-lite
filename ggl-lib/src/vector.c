// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/vector.h"
#include "ggl/error.h"
#include "ggl/list.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <string.h>

GglError ggl_obj_vec_push(GglObjVec *vector, GglObject object) {
    if (vector->list.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("Pushed to %p.", vector);
    vector->list.items[vector->list.len] = object;
    vector->list.len++;
    return GGL_ERR_OK;
}

void ggl_obj_vec_chain_push(
    GglError *err, GglObjVec *vector, GglObject object
) {
    if (*err == GGL_ERR_OK) {
        *err = ggl_obj_vec_push(vector, object);
    }
}

GglError ggl_obj_vec_pop(GglObjVec *vector, GglObject *out) {
    if (vector->list.len == 0) {
        return GGL_ERR_RANGE;
    }
    if (out != NULL) {
        *out = vector->list.items[vector->list.len - 1];
    }
    GGL_LOGT("Popped from %p.", vector);

    vector->list.len--;
    return GGL_ERR_OK;
}

GglError ggl_obj_vec_append(GglObjVec *vector, GglList list) {
    if (vector->capacity - vector->list.len < list.len) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("Appended to %p.", vector);
    memcpy(
        &vector->list.items[vector->list.len],
        list.items,
        list.len * sizeof(GglObject)
    );
    vector->list.len += list.len;
    return GGL_ERR_OK;
}

void ggl_obj_vec_chain_append(GglError *err, GglObjVec *vector, GglList list) {
    if (*err == GGL_ERR_OK) {
        *err = ggl_obj_vec_append(vector, list);
    }
}

GglError ggl_kv_vec_push(GglKVVec *vector, GglKV kv) {
    if (vector->map.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("Pushed to %p.", vector);
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
    GGL_LOGT("Pushed to %p.", vector);
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
    GGL_LOGT("Appended to %p.", vector);
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

GglBuffer ggl_byte_vec_remaining_capacity(GglByteVec vector) {
    return (GglBuffer) { .data = &vector.buf.data[vector.buf.len],
                         .len = vector.capacity - vector.buf.len };
}

GglError ggl_buf_vec_push(GglBufVec *vector, GglBuffer buf) {
    if (vector->buf_list.len >= vector->capacity) {
        return GGL_ERR_NOMEM;
    }
    GGL_LOGT("Pushed to %p.", vector);
    vector->buf_list.bufs[vector->buf_list.len] = buf;
    vector->buf_list.len++;
    return GGL_ERR_OK;
}

void ggl_buf_vec_chain_push(GglError *err, GglBufVec *vector, GglBuffer buf) {
    if (*err == GGL_ERR_OK) {
        *err = ggl_buf_vec_push(vector, buf);
    }
}

GglError ggl_buf_vec_append_list(GglBufVec *vector, GglList list) {
    GGL_LIST_FOREACH(item, list) {
        if (item->type != GGL_TYPE_BUF) {
            return GGL_ERR_INVALID;
        }
        GglError ret = ggl_buf_vec_push(vector, item->buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

void ggl_buf_vec_chain_append_list(
    GglError *err, GglBufVec *vector, GglList list
) {
    if (*err == GGL_ERR_OK) {
        *err = ggl_buf_vec_append_list(vector, list);
    }
}
