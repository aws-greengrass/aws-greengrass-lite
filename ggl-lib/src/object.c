// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/object.h"
#include "ggl/alloc.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include <assert.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_obj_deep_copy(GglObject *obj, GglAlloc *alloc) {
    switch (obj->type) {
    case GGL_TYPE_NULL:
    case GGL_TYPE_BOOLEAN:
    case GGL_TYPE_I64:
    case GGL_TYPE_F64: {
        return GGL_ERR_OK;
    }
    case GGL_TYPE_BUF: {
        uint8_t *new_mem = GGL_ALLOCN(alloc, uint8_t, obj->buf.len);
        if (new_mem == NULL) {
            GGL_LOGE("object", "Insufficient memory when making deep copy.");
            return GGL_ERR_NOMEM;
        }
        memcpy(new_mem, obj->buf.data, obj->buf.len);
        obj->buf.data = new_mem;
        return GGL_ERR_OK;
    }
    case GGL_TYPE_LIST: {
        GglObject *new_mem = GGL_ALLOCN(alloc, GglObject, obj->list.len);
        if (new_mem == NULL) {
            GGL_LOGE("object", "Insufficient memory when making deep copy.");
            return GGL_ERR_NOMEM;
        }
        memcpy(new_mem, obj->list.items, obj->list.len * sizeof(GglObject));
        obj->list.items = new_mem;
        for (size_t i = 0; i < obj->list.len; i++) {
            GglError ret = ggl_obj_deep_copy(&new_mem[i], alloc);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
        return GGL_ERR_OK;
    }
    case GGL_TYPE_MAP: {
        GglKV *new_mem = GGL_ALLOCN(alloc, GglKV, obj->map.len);
        if (new_mem == NULL) {
            GGL_LOGE("object", "Insufficient memory when making deep copy.");
            return GGL_ERR_NOMEM;
        }
        memcpy(new_mem, obj->map.pairs, obj->map.len * sizeof(GglKV));
        obj->map.pairs = new_mem;
        for (size_t i = 0; i < obj->map.len; i++) {
            uint8_t *key_mem = GGL_ALLOCN(alloc, uint8_t, new_mem[i].key.len);
            if (key_mem == NULL) {
                GGL_LOGE(
                    "object", "Insufficient memory when making deep copy."
                );
                return GGL_ERR_NOMEM;
            }
            memcpy(key_mem, new_mem[i].key.data, new_mem[i].key.len);
            new_mem[i].key.data = key_mem;

            GglError ret = ggl_obj_deep_copy(&new_mem[i].val, alloc);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        }
        return GGL_ERR_OK;
    }
    }

    assert(false);
    return GGL_ERR_FAILURE;
}
