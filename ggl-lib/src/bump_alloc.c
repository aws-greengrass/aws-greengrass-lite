// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/bump_alloc.h"
#include "ggl/alloc.h"
#include "ggl/log.h"
#include "ggl/object.h"

static void *bump_alloc_alloc(GglAlloc *alloc, size_t size, size_t alignment) {
    GglBumpAlloc *ctx = (GglBumpAlloc *) alloc;

    size_t pad = (alignment - (ctx->index % alignment)) % alignment;
    size_t idx = ctx->index + pad;

    if (pad > 0) {
        GGL_LOGD("[%p] Need %zu padding.", (void *) alloc, pad);
    }

    if (idx + size > ctx->buf.len) {
        return NULL;
    }

    ctx->index = idx + size;
    return &ctx->buf.data[idx];
}

GglBumpAlloc ggl_bump_alloc_init(GglBuffer buf) {
    return (GglBumpAlloc) {
        .alloc = { .ALLOC = bump_alloc_alloc, .FREE = NULL },
        .buf = buf,
        .index = 0,
    };
}
