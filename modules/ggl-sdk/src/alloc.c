// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggl/alloc.h>
#include <ggl/log.h>
#include <stddef.h>

void *ggl_alloc(GglAlloc alloc, size_t size, size_t alignment) {
    void *ret = NULL;

    if ((alloc.VTABLE != NULL) && (alloc.VTABLE->ALLOC != NULL)) {
        ret = alloc.VTABLE->ALLOC(alloc.ctx, size, alignment);
    }

    if (ret == NULL) {
        GGL_LOGW(
            "[%p:%p] Failed alloc %zu bytes.",
            (void *) alloc.VTABLE,
            (void *) alloc.ctx,
            size
        );
    } else {
        GGL_LOGT(
            "[%p:%p] alloc %p, len %zu.",
            (void *) alloc.VTABLE,
            (void *) alloc.ctx,
            ret,
            size
        );
    }

    return ret;
}

void ggl_free(GglAlloc alloc, void *ptr) {
    GGL_LOGT("[%p:%p] Free %p", (void *) alloc.VTABLE, (void *) alloc.ctx, ptr);

    if ((ptr != NULL) && (alloc.VTABLE != NULL)
        && (alloc.VTABLE->FREE != NULL)) {
        alloc.VTABLE->FREE(alloc.ctx, ptr);
    }
}
