// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CLEANUP_H
#define GGL_CLEANUP_H

//! Macros for automatic resource cleanup

#include <sys/types.h>
#include <ggl/macro_util.h>
#include <pthread.h>
#include <stdlib.h>

#define GGL_CLEANUP_ID(ident, fn, exp) \
    __attribute__((cleanup(fn))) typeof(exp) ident = (exp);
#define GGL_CLEANUP(fn, exp) \
    GGL_CLEANUP_ID(GGL_MACRO_PASTE(cleanup_, __LINE__), fn, exp)

static inline void cleanup_free(void *p) {
    free(*(void **) p);
}

static inline void cleanup_pthread_mtx_unlock(pthread_mutex_t **mtx) {
    if (*mtx != NULL) {
        pthread_mutex_unlock(*mtx);
    }
}

// NOLINTBEGIN(bugprone-macro-parentheses)
#define GGL_MTX_SCOPE_GUARD_ID(ident, mtx) \
    __attribute__((cleanup(cleanup_pthread_mtx_unlock))) \
    pthread_mutex_t *ident \
        = mtx; \
    pthread_mutex_lock(ident);
// NOLINTEND(bugprone-macro-parentheses)

#define GGL_MTX_SCOPE_GUARD(mtx) \
    GGL_MTX_SCOPE_GUARD_ID(GGL_MACRO_PASTE(ggl_mtx_lock_, __LINE__), mtx)

#endif
