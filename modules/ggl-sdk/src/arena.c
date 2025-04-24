// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

void *ggl_arena_alloc(GglArena *arena, size_t size, size_t alignment) {
    if (arena == NULL) {
        GGL_LOGD("Attempted allocation with NULL arena.");
        return NULL;
    }

    // alignment must be power of 2
    assert((alignment > 0) && ((alignment & (alignment - 1)) == 0));
    assert(alignment < UINT32_MAX);
    // Allocation can't exceed ptrdiff_t, and this is likely a bug.
    assert(size <= PTRDIFF_MAX);

    uint32_t align = (uint32_t) alignment;
    uint32_t pad = (align - (arena->index & (align - 1))) & (align - 1);

    if (pad > 0) {
        GGL_LOGD("[%p] Need %" PRIu32 " padding.", arena, pad);
    }

    if (pad > arena->capacity - arena->index) {
        GGL_LOGD(
            "[%p] Insufficient memory for padding; returning NULL.", arena
        );
        return NULL;
    }

    uint32_t idx = arena->index + pad;

    if (size > arena->capacity - idx) {
        GGL_LOGD(
            "[%p] Insufficient memory to alloc %zu; returning NULL.",
            arena,
            size + pad
        );
        return NULL;
    }

    arena->index = idx + (uint32_t) size;
    return &arena->mem[idx];
}

GglError ggl_arena_resize_last(
    GglArena *arena, const void *ptr, size_t old_size, size_t size
) {
    assert(arena != NULL);
    assert(ptr != NULL);
    assert(old_size < UINT32_MAX);
    assert(old_size <= PTRDIFF_MAX);
    assert(size <= PTRDIFF_MAX);

    if (!ggl_arena_owns(arena, ptr)) {
        GGL_LOGE("[%p] Resize ptr %p not owned.", arena, ptr);
        assert(false);
        return GGL_ERR_INVALID;
    }

    uint32_t idx = (uint32_t) ((uintptr_t) ptr - (uintptr_t) arena->mem);

    if (idx > arena->index) {
        GGL_LOGE("[%p] Resize ptr %p out of allocated range.", arena, ptr);
        assert(false);
        return GGL_ERR_INVALID;
    }

    if (arena->index - idx != old_size) {
        GGL_LOGE(
            "[%p] Resize ptr %p + size %zu does not match allocation index",
            arena,
            ptr,
            old_size
        );
        return GGL_ERR_INVALID;
    }

    if (size > arena->capacity - idx) {
        GGL_LOGD(
            "[%p] Insufficient memory to resize %p to %zu.", arena, ptr, size
        );
        return GGL_ERR_NOMEM;
    }

    arena->index = idx + (uint32_t) size;
    return GGL_ERR_OK;
}

bool ggl_arena_owns(const GglArena *arena, const void *ptr) {
    if (arena == NULL) {
        return false;
    }
    uintptr_t mem_int = (uintptr_t) arena->mem;
    uintptr_t ptr_int = (uintptr_t) ptr;
    return (ptr_int >= mem_int) && (ptr_int < mem_int + arena->capacity);
}

GglBuffer ggl_arena_alloc_rest(GglArena *arena) {
    if (arena == NULL) {
        return (GglBuffer) { 0 };
    }
    size_t remaining = arena->capacity - arena->index;
    uint8_t *data = GGL_ARENA_ALLOCN(arena, uint8_t, remaining);
    assert(data != NULL);
    return (GglBuffer) { .data = data, .len = remaining };
}

GglError ggl_arena_claim_buf(GglBuffer *buf, GglArena *arena) {
    assert(buf != NULL);

    if (ggl_arena_owns(arena, buf->data)) {
        return GGL_ERR_OK;
    }

    if (buf->len == 0) {
        buf->data = NULL;
        return GGL_ERR_OK;
    }

    uint8_t *new_mem = GGL_ARENA_ALLOCN(arena, uint8_t, buf->len);
    if (new_mem == NULL) {
        GGL_LOGE("Insufficient memory when cloning buffer into %p.", arena);
        return GGL_ERR_NOMEM;
    }

    memcpy(new_mem, buf->data, buf->len);
    buf->data = new_mem;
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError claim_list(GglList *list, GglArena *arena) {
    if (!ggl_arena_owns(arena, list->items)) {
        if (list->len == 0) {
            list->items = NULL;
            return GGL_ERR_OK;
        }

        GglObject *new_mem = GGL_ARENA_ALLOCN(arena, GglObject, list->len);
        if (new_mem == NULL) {
            GGL_LOGE("Insufficient memory when cloning list into %p.", arena);
            return GGL_ERR_NOMEM;
        }
        memcpy(new_mem, list->items, list->len * sizeof(GglObject));
        list->items = new_mem;
    }

    GGL_LIST_FOREACH(elem, *list) {
        GglError ret = ggl_arena_claim_obj(elem, arena);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError claim_map(GglMap *map, GglArena *arena) {
    if (!ggl_arena_owns(arena, map->pairs)) {
        if (map->len == 0) {
            map->pairs = NULL;
            return GGL_ERR_OK;
        }

        GglKV *new_mem = GGL_ARENA_ALLOCN(arena, GglKV, map->len);
        if (new_mem == NULL) {
            GGL_LOGE("Insufficient memory when cloning map into %p.", arena);
            return GGL_ERR_NOMEM;
        }
        memcpy(new_mem, map->pairs, map->len * sizeof(GglKV));
        map->pairs = new_mem;
    }

    GGL_MAP_FOREACH(kv, *map) {
        GglError ret = ggl_arena_claim_buf(&kv->key, arena);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_arena_claim_obj(&kv->val, arena);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_arena_claim_obj(GglObject *obj, GglArena *arena) {
    assert(obj != NULL);

    switch (ggl_obj_type(*obj)) {
    case GGL_TYPE_NULL:
    case GGL_TYPE_BOOLEAN:
    case GGL_TYPE_I64:
    case GGL_TYPE_F64:
        return GGL_ERR_OK;
    case GGL_TYPE_BUF: {
        GglBuffer buf = ggl_obj_into_buf(*obj);
        GglError ret = ggl_arena_claim_buf(&buf, arena);
        *obj = ggl_obj_buf(buf);
        return ret;
    }
    case GGL_TYPE_LIST: {
        GglList list = ggl_obj_into_list(*obj);
        GglError ret = claim_list(&list, arena);
        *obj = ggl_obj_list(list);
        return ret;
    }
    case GGL_TYPE_MAP: {
        GglMap map = ggl_obj_into_map(*obj);
        GglError ret = claim_map(&map, arena);
        *obj = ggl_obj_map(map);
        return ret;
    }
    }

    assert(false);
    return GGL_ERR_FAILURE;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError claim_list_bufs(GglList list, GglArena *arena) {
    GGL_LIST_FOREACH(elem, list) {
        GglError ret = ggl_arena_claim_obj_bufs(elem, arena);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
static GglError claim_map_bufs(GglMap map, GglArena *arena) {
    GGL_MAP_FOREACH(kv, map) {
        GglError ret = ggl_arena_claim_buf(&kv->key, arena);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_arena_claim_obj_bufs(&kv->val, arena);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_arena_claim_obj_bufs(GglObject *obj, GglArena *arena) {
    assert(obj != NULL);

    switch (ggl_obj_type(*obj)) {
    case GGL_TYPE_NULL:
    case GGL_TYPE_BOOLEAN:
    case GGL_TYPE_I64:
    case GGL_TYPE_F64:
        return GGL_ERR_OK;
    case GGL_TYPE_BUF: {
        GglBuffer buf = ggl_obj_into_buf(*obj);
        GglError ret = ggl_arena_claim_buf(&buf, arena);
        *obj = ggl_obj_buf(buf);
        return ret;
    }
    case GGL_TYPE_LIST:
        return claim_list_bufs(ggl_obj_into_list(*obj), arena);
    case GGL_TYPE_MAP:
        return claim_map_bufs(ggl_obj_into_map(*obj), arena);
    }

    assert(false);
    return GGL_ERR_FAILURE;
}
