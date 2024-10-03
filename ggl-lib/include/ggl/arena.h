// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_ARENA_H
#define GGL_ARENA_H

//! Arena allocation

#include "error.h"
#include "object.h"
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Arena allocator backed by fixed buffer
typedef struct {
    uint8_t *const MEM;
    const uint32_t CAPACITY;
    uint32_t index;
} GglArena;

/// Obtain an initialized `GglAlloc` backed by `buf`.
static inline GglArena ggl_arena_init(GglBuffer buf) {
    return (GglArena) { .MEM = buf.data,
                        .CAPACITY = buf.len <= UINT32_MAX ? (uint32_t) buf.len
                                                          : UINT32_MAX };
}

/// Allocate a `type` from an arena.
#define GGL_ARENA_ALLOC(arena, type) \
    (typeof(type) *) ggl_arena_alloc(arena, sizeof(type), alignof(type))
/// Allocate `n` units of `type` from an arena.
#define GGL_ARENA_ALLOCN(arena, type, n) \
    (typeof(type) *) ggl_arena_alloc(arena, (n) * sizeof(type), alignof(type))

/// Allocate `size` bytes with given alignment from an arena.
void *ggl_arena_alloc(GglArena *arena, size_t size, size_t alignment);

/// Resize ptr's allocation (must be the last allocated ptr).
GglError ggl_arena_resize_last(GglArena *arena, const void *ptr, size_t size);

/// Returns true if arena's mem contains ptr.
bool ggl_arena_owns(const GglArena *arena, const void *ptr);

#endif
