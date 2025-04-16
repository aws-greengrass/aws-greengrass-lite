// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/object.h"
#include "ggl/alloc.h"
#include "ggl/buffer.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include <assert.h>
#include <string.h>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
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

static GglError deep_copy_buf(GglBuffer *buf, GglAlloc *alloc) {
    if (buf->len == 0) {
        return GGL_ERR_OK;
    }
    uint8_t *new_mem = GGL_ALLOCN(alloc, uint8_t, buf->len);
    if (new_mem == NULL) {
        GGL_LOGE("Insufficient memory when making deep copy.");
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
        GGL_LOGE("Insufficient memory when making deep copy.");
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
        GGL_LOGE("Insufficient memory when making deep copy.");
        return GGL_ERR_NOMEM;
    }
    memcpy(new_mem, map->pairs, map->len * sizeof(GglKV));
    map->pairs = new_mem;
    for (size_t i = 0; i < map->len; i++) {
        uint8_t *key_mem = GGL_ALLOCN(alloc, uint8_t, new_mem[i].key.len);
        if (key_mem == NULL) {
            GGL_LOGE("Insufficient memory when making deep copy.");
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
    switch (ggl_obj_type(*obj)) {
    case GGL_TYPE_NULL:
    case GGL_TYPE_BOOLEAN:
    case GGL_TYPE_I64:
    case GGL_TYPE_F64:
        return GGL_ERR_OK;
    case GGL_TYPE_BUF: {
        GglBuffer buf = ggl_obj_into_buf(*obj);
        GglError ret = deep_copy_buf(&buf, alloc);
        *obj = ggl_obj_buf(buf);
        return ret;
    }
    case GGL_TYPE_LIST: {
        GglList list = ggl_obj_into_list(*obj);
        GglError ret = deep_copy_list(&list, alloc);
        *obj = ggl_obj_list(list);
        return ret;
    }
    case GGL_TYPE_MAP: {
        GglMap map = ggl_obj_into_map(*obj);
        GglError ret = deep_copy_map(&map, alloc);
        *obj = ggl_obj_map(map);
        return ret;
    }
    }

    assert(false);
    return GGL_ERR_FAILURE;
}

static GglError buffer_copy_buf(GglBuffer *buf, GglAlloc *alloc) {
    if (buf->len == 0) {
        return GGL_ERR_OK;
    }
    if (alloc == NULL) {
        GGL_LOGE("Null allocator when copying buffers.");
        return GGL_ERR_NOMEM;
    }
    uint8_t *new_mem = GGL_ALLOCN(alloc, uint8_t, buf->len);
    if (new_mem == NULL) {
        GGL_LOGE("Insufficient memory when copying buffers.");
        return GGL_ERR_NOMEM;
    }
    memcpy(new_mem, buf->data, buf->len);
    buf->data = new_mem;
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError buffer_copy_list(GglList *list, GglAlloc *alloc) {
    for (size_t i = 0; i < list->len; i++) {
        GglError ret = ggl_obj_buffer_copy(&list->items[i], alloc);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError buffer_copy_map(GglMap *map, GglAlloc *alloc) {
    for (size_t i = 0; i < map->len; i++) {
        GglKV *kv = &map->pairs[i];
        if (alloc == NULL) {
            GGL_LOGE("Null allocator when copying buffers.");
            return GGL_ERR_NOMEM;
        }
        uint8_t *mem = GGL_ALLOCN(alloc, uint8_t, kv->key.len);
        if (mem == NULL) {
            GGL_LOGE("Insufficient memory when copying buffers.");
            return GGL_ERR_NOMEM;
        }
        memcpy(mem, kv->key.data, kv->key.len);
        kv->key.data = mem;

        GglError ret = ggl_obj_buffer_copy(&kv->val, alloc);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_obj_buffer_copy(GglObject *obj, GglAlloc *alloc) {
    switch (ggl_obj_type(*obj)) {
    case GGL_TYPE_NULL:
    case GGL_TYPE_BOOLEAN:
    case GGL_TYPE_I64:
    case GGL_TYPE_F64:
        return GGL_ERR_OK;
    case GGL_TYPE_BUF: {
        GglBuffer buf = ggl_obj_into_buf(*obj);
        GglError ret = buffer_copy_buf(&buf, alloc);
        *obj = ggl_obj_buf(buf);
        return ret;
    }
    case GGL_TYPE_LIST: {
        GglList list = ggl_obj_into_list(*obj);
        GglError ret = buffer_copy_list(&list, alloc);
        *obj = ggl_obj_list(list);
        return ret;
    }
    case GGL_TYPE_MAP: {
        GglMap map = ggl_obj_into_map(*obj);
        GglError ret = buffer_copy_map(&map, alloc);
        *obj = ggl_obj_map(map);
        return ret;
    }
    }

    assert(false);
    return GGL_ERR_FAILURE;
}
