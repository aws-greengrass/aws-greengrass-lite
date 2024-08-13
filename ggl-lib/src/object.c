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

static GglError deep_copy_buf(GglBuffer *buf, GglAlloc *alloc) {
    if (buf->len == 0) {
        return GGL_ERR_OK;
    }
    uint8_t *new_mem = GGL_ALLOCN(alloc, uint8_t, buf->len);
    if (new_mem == NULL) {
        GGL_LOGE("object", "Insufficient memory when making deep copy.");
        return GGL_ERR_NOMEM;
    }
    memcpy(new_mem, buf->data, buf->len);
    buf->data = new_mem;
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError deep_copy_list(GglList *list, GglAlloc *alloc) {
    if (list->len == 0) {
        return GGL_ERR_OK;
    }
    GglObject *new_mem = GGL_ALLOCN(alloc, GglObject, list->len);
    if (new_mem == NULL) {
        GGL_LOGE("object", "Insufficient memory when making deep copy.");
        return GGL_ERR_NOMEM;
    }
    memcpy(new_mem, list->items, list->len * sizeof(GglObject));
    list->items = new_mem;
    for (size_t i = 0; i < list->len; i++) {
        GglError ret = ggl_obj_deep_copy(&new_mem[i], alloc);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError deep_copy_map(GglMap *map, GglAlloc *alloc) {
    if (map->len == 0) {
        return GGL_ERR_OK;
    }
    GglKV *new_mem = GGL_ALLOCN(alloc, GglKV, map->len);
    if (new_mem == NULL) {
        GGL_LOGE("object", "Insufficient memory when making deep copy.");
        return GGL_ERR_NOMEM;
    }
    memcpy(new_mem, map->pairs, map->len * sizeof(GglKV));
    map->pairs = new_mem;
    for (size_t i = 0; i < map->len; i++) {
        uint8_t *key_mem = GGL_ALLOCN(alloc, uint8_t, new_mem[i].key.len);
        if (key_mem == NULL) {
            GGL_LOGE("object", "Insufficient memory when making deep copy.");
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

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_obj_deep_copy(GglObject *obj, GglAlloc *alloc) {
    switch (obj->type) {
    case GGL_TYPE_NULL:
    case GGL_TYPE_BOOLEAN:
    case GGL_TYPE_I64:
    case GGL_TYPE_F64: {
        return GGL_ERR_OK;
    }
    case GGL_TYPE_BUF:
        return deep_copy_buf(&obj->buf, alloc);
    case GGL_TYPE_LIST:
        return deep_copy_list(&obj->list, alloc);
    case GGL_TYPE_MAP:
        return deep_copy_map(&obj->map, alloc);
    }

    assert(false);
    return GGL_ERR_FAILURE;
}
