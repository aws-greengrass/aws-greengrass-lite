// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/alloc.h"
#include "ggl/log.h"
#include <assert.h>

void *ggl_alloc(GglAlloc *alloc, size_t size, size_t alignment) {
    assert((alloc != NULL) && (alloc->ALLOC != NULL));

    void *ret = alloc->ALLOC(alloc, size, alignment);

    if (ret == NULL) {
        GGL_LOGW("[%p] Failed alloc %zu bytes.", (void *) alloc, size);
    } else {
        GGL_LOGT("[%p] alloc %p, len %zu.", (void *) alloc, ret, size);
    }

    return ret;
}

void ggl_free(GglAlloc *alloc, void *ptr) {
    assert(alloc != NULL);

    GGL_LOGT("[%p] Free %p", (void *) alloc, ptr);

    if ((alloc->FREE != NULL) && (ptr != NULL)) {
        alloc->FREE(alloc, ptr);
    }
}
