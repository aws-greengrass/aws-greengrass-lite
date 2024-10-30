// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BUMP_ALLOC_H
#define GGL_BUMP_ALLOC_H

//! A simple bump allocator

#include "alloc.h"
#include <ggl/buffer.h>
#include <stddef.h>

/// Alloc-only allocator backed by a fixed buffer.
typedef struct {
    GglAlloc alloc;
    GglBuffer buf;
    size_t index;
} GglBumpAlloc;

/// Obtain an initialized `Ggl_BumpAlloc` backed by `buf`.
GglBumpAlloc ggl_bump_alloc_init(GglBuffer buf);

#endif
