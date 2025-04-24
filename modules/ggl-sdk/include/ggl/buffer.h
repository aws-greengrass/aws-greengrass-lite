// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BUFFER_H
#define GGL_BUFFER_H

//! Buffer utilities.

#include <ggl/attr.h>
#include <ggl/error.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __COVERITY__
#define GGL_DISABLE_MACRO_TYPE_CHECKING
#endif

/// A fixed buffer of bytes. Possibly a string.
typedef struct {
    uint8_t *data;
    size_t len;
} GglBuffer;

/// An array of `GglBuffer`
typedef struct {
    GglBuffer *bufs;
    size_t len;
} GglBufList;

// The only way to guarantee a string literal is with assignment; this could be
// done with a statement expression but those are not allowed at file context.

#define GGL_STR_UNCHECKED(strlit) \
    ((GglBuffer) { .data = (uint8_t *) (strlit), .len = sizeof(strlit) - 1U })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
/// Create buffer literal from a string literal.
#define GGL_STR(strlit) \
    _Generic( \
        (&(strlit)), \
        char(*)[]: GGL_STR_UNCHECKED(strlit), \
        const char(*)[]: GGL_STR_UNCHECKED(strlit) \
    )
#else
#define GGL_STR GGL_STR_UNCHECKED
#endif

// generic function on pointer is to validate parameter is array and not ptr.
// On systems where char == uint8_t, this won't warn on string literal.

#define GGL_BUF_UNCHECKED(...) \
    ((GglBuffer) { .data = (__VA_ARGS__), .len = sizeof(__VA_ARGS__) })

#ifndef GGL_DISABLE_MACRO_TYPE_CHECKING
/// Create buffer literal from a byte array.
#define GGL_BUF(...) \
    _Generic((&(__VA_ARGS__)), uint8_t(*)[]: GGL_BUF_UNCHECKED(__VA_ARGS__))
#else
#define GGL_BUF GGL_BUF_UNCHECKED
#endif

/// Create buffer list literal from buffer literals.
#define GGL_BUF_LIST(...) \
    (GglBufList) { \
        .bufs = (GglBuffer[]) { __VA_ARGS__ }, \
        .len = (sizeof((GglBuffer[]) { __VA_ARGS__ })) / (sizeof(GglBuffer)) \
    }

// NOLINTBEGIN(bugprone-macro-parentheses)
/// Loop over the objects in a list.
#define GGL_BUF_LIST_FOREACH(name, list) \
    for (GglBuffer *name = (list).bufs; name < &(list).bufs[(list).len]; \
         name = &name[1])
// NOLINTEND(bugprone-macro-parentheses)

/// Convert null-terminated string to buffer
GglBuffer ggl_buffer_from_null_term(char *str) NONNULL(1)
    NULL_TERMINATED_STRING_ARG(1);

/// Returns whether two buffers have identical content.
bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2);

/// Returns whether the buffer has the given prefix.
bool ggl_buffer_has_prefix(GglBuffer buf, GglBuffer prefix);

/// Removes a prefix. Returns whether the prefix was removed.
bool ggl_buffer_remove_prefix(GglBuffer *buf, GglBuffer prefix) NONNULL(1);

/// Returns whether the buffer has the given suffix.
bool ggl_buffer_has_suffix(GglBuffer buf, GglBuffer suffix);

/// Removes a suffix. Returns whether the suffix was removed.
bool ggl_buffer_remove_suffix(GglBuffer *buf, GglBuffer suffix) NONNULL(1);

/// Returns whether the buffer contains the given substring.
/// Outputs start index if non-null.
bool ggl_buffer_contains(GglBuffer buf, GglBuffer substring, size_t *start);

/// Returns substring of buffer from start to end.
/// The result is the overlap between the start to end range and the input
/// bounds.
GglBuffer ggl_buffer_substr(GglBuffer buf, size_t start, size_t end);

/// Parse an integer from a string
GglError ggl_str_to_int64(GglBuffer str, int64_t *value);

#endif
