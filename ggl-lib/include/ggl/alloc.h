/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_ALLOC_H
#define GGL_ALLOC_H

/*! Generic allocator interface */

#include <stdalign.h>
#include <stddef.h>

/** Allocator vtable. Embedded into allocator structs. */
typedef struct GglAlloc {
    void *(*const ALLOC)(struct GglAlloc *ctx, size_t size, size_t alignment);
    void (*const FREE)(struct GglAlloc *ctx, void *ptr);
} GglAlloc;

/** Allocate memory from an allocator.
 * Prefer `GGL_ALLOC` or `GGL_ALLOCN`. */
void *ggl_alloc(GglAlloc *alloc, size_t size, size_t alignment);
/** Free memory allocated from an allocator.
 * Prefer `GGL_AUTOFREE` for scope-bound values. */
void ggl_free(GglAlloc *alloc, void *ptr);

/** Allocate a `type` from an allocator. */
#define GGL_ALLOC(alloc, type) \
    (type *) ggl_alloc(alloc, sizeof(type), alignof(type))
/** Allocate `n` units of `type` from an allocator. */
#define GGL_ALLOCN(alloc, type, n) \
    (type *) ggl_alloc(alloc, (n) * sizeof(type), alignof(type))

#endif
