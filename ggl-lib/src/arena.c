// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/arena.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

void *ggl_arena_alloc(GglArena *arena, size_t size, size_t alignment) {
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

    if (pad > arena->CAPACITY - arena->index) {
        GGL_LOGD(
            "[%p] Insufficient memory for padding; returning NULL.", arena
        );
        return NULL;
    }

    uint32_t idx = arena->index + pad;

    if (size > arena->CAPACITY - idx) {
        GGL_LOGD(
            "[%p] Insufficient memory to alloc %zu; returning NULL.",
            arena,
            size + pad
        );
        return NULL;
    }

    arena->index = idx + (uint32_t) size;
    return &arena->MEM[idx];
}

GglError ggl_arena_resize_last(
    GglArena *arena, const void *ptr, size_t old_size, size_t size
) {
    assert(old_size < UINT32_MAX);
    assert(old_size <= PTRDIFF_MAX);
    assert(size <= PTRDIFF_MAX);

    if (!ggl_arena_owns(arena, ptr)) {
        GGL_LOGE("[%p] Resize ptr %p not owned.", arena, ptr);
        assert(false);
        return GGL_ERR_INVALID;
    }

    uint32_t idx = (uint32_t) ((uintptr_t) ptr - (uintptr_t) arena->MEM);

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

    if (size > arena->CAPACITY - idx) {
        GGL_LOGD(
            "[%p] Insufficient memory to resize %p to %zu.", arena, ptr, size
        );
        return GGL_ERR_NOMEM;
    }

    arena->index = idx + (uint32_t) size;
    return GGL_ERR_OK;
}

bool ggl_arena_owns(const GglArena *arena, const void *ptr) {
    uintptr_t mem_int = (uintptr_t) arena->MEM;
    uintptr_t ptr_int = (uintptr_t) ptr;
    return (ptr_int >= mem_int) && (ptr_int < mem_int + arena->CAPACITY);
}
