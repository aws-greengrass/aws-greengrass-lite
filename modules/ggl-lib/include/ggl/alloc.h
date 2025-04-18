// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_ALLOC_H
#define GGL_ALLOC_H

//! Generic allocator interface

#include <ggl/attr.h>
#include <stdalign.h>
#include <stddef.h>

/// Allocator vtable.
typedef struct {
    void *(*const ALLOC)(void *ctx, size_t size, size_t alignment);
    void (*const FREE)(void *ctx, void *ptr);
} DESIGNATED_INIT GglAllocVtable;

typedef struct {
    const GglAllocVtable *const VTABLE;
    void *ctx;
} DESIGNATED_INIT GglAlloc;

/// Allocate a single `type` from an allocator.
#define GGL_ALLOC(alloc, type) \
    (typeof(type) *) ggl_alloc(alloc, sizeof(type), alignof(type))

/// Allocate `n` units of `type` from an allocator.
#define GGL_ALLOCN(alloc, type, n) \
    (typeof(type) *) ggl_alloc(alloc, (n) * sizeof(type), alignof(type))

/// Allocate memory from an allocator.
/// Prefer `GGL_ALLOC` or `GGL_ALLOCN`.
void *ggl_alloc(GglAlloc alloc, size_t size, size_t alignment);

/// Free memory allocated from an allocator.
void ggl_free(GglAlloc alloc, void *ptr);

#endif
