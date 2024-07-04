/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_DEFER_H
#define GGL_DEFER_H

/*! Macros for automatic resource cleanup */

#include "alloc.h"
#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

/** Run clean up for `fn` when this falls out of scope. */
#define GGL_DEFER(fn, ...) GGL_DEFER_PRIV(fn, __VA_ARGS__, )

#define GGL_DEFER_PRIV(fn, var, ...) \
    GglDeferArgType__##fn ggl_defer_check_type__##var __attribute__((unused)) \
    = var; \
    __attribute__((cleanup(ggl_defer__##fn))) \
    GglDeferRecord__##fn ggl_defer_record__##var \
        = { (GglDeferArgType__##fn *) &var, __VA_ARGS__ }; \
    static void (*const ggl_defer_fn__##var)(GglDeferRecord__##fn *) \
        __attribute__((unused)) \
        = ggl_defer__##fn; \
    static void (*const ggl_defer_cancel_fn__##var)(GglDeferRecord__##fn *) \
        __attribute__((unused)) \
        = ggl_cancel_defer__##fn

/** Cancel cleanup for `id`. */
#define GGL_DEFER_CANCEL(id) ggl_defer_cancel_fn__##id(&ggl_defer_record__##id)

/** Immediately cleanup `id`. */
#define GGL_DEFER_FORCE(id) \
    ggl_defer_fn__##id(&ggl_defer_record__##id); \
    GGL_DEFER_CANCEL(id)

// NOLINTBEGIN(bugprone-macro-parentheses)
/** Enable defer for a function with one parameter. */
#define GGL_DEFINE_DEFER(fn, type, name, cleanup) \
    typedef type GglDeferArgType__##fn; \
    typedef type *GglDeferRecord__##fn; \
    static inline void ggl_defer__##fn(type **defer_record_ptr) { \
        type *name = *defer_record_ptr; \
        if (name != NULL) { \
            cleanup; \
        } \
    } \
    static inline void ggl_cancel_defer__##fn(type **defer_record_ptr) { \
        *defer_record_ptr = NULL; \
    }
// NOLINTEND(bugprone-macro-parentheses)

// Common deferred functions

/** Enable defer for closing file descriptors. */
GGL_DEFINE_DEFER(close, int, fd, if (*fd >= 0) close(*fd))

/** Enable defer for freeing system allocated pointers. */
GGL_DEFINE_DEFER(free, void *, p, free(*p))

/** Enable defer for unlocking mutexes. */
GGL_DEFINE_DEFER(
    pthread_mutex_unlock, pthread_mutex_t, mut, pthread_mutex_unlock(mut)
)

/** Enable defer for `ggl_free` */

// NOLINTNEXTLINE(readability-identifier-naming)
typedef void *GglDeferArgType__ggl_free;

typedef struct {
    void **ptr;
    GglAlloc *ctx;
} GglDeferRecord__ggl_free; // NOLINT(readability-identifier-naming)

static inline void ggl_defer__ggl_free(GglDeferRecord__ggl_free *record) {
    ggl_free(record->ctx, *record->ptr);
}

static inline void ggl_cancel_defer__ggl_free(GglDeferRecord__ggl_free *record
) {
    *record->ptr = NULL;
}

#endif
