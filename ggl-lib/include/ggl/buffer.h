// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BUFFER_H
#define GGL_BUFFER_H

//! Buffer utilities.

#include "object.h"
#include <ggl/error.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// An array of `GglBuffer`
typedef struct {
    GglBuffer *bufs;
    size_t len;
} GglBufList;

/// Create buffer list literal from buffer literals.
#define GGL_BUF_LIST(...) \
    (GglBufList) { \
        .bufs = (GglBuffer[]) { __VA_ARGS__ }, \
        .len = (sizeof((GglBuffer[]) { __VA_ARGS__ })) / (sizeof(GglBuffer)) \
    }

/// Convert null-terminated string to buffer
GglBuffer ggl_buffer_from_null_term(char *str);

/// Returns whether two buffers have identical content.
bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2);

/// Returns whether the buffer has the given prefix.
bool ggl_buffer_has_prefix(GglBuffer buf, GglBuffer prefix);

/// Returns whether the buffer has the given suffix.
bool ggl_buffer_has_suffix(GglBuffer buf, GglBuffer suffix);

/// Returns whether the buffer contains the given substring.
/// Outputs start and end index.
bool ggl_buffer_contains(GglBuffer buf, GglBuffer substring, size_t *start);

/// Returns substring of buffer from start to end.
/// The result is the overlap between the start to end range and the input
/// bounds.
GglBuffer ggl_buffer_substr(GglBuffer buf, size_t start, size_t end);

/// Parse an integer from a string
GglError ggl_str_to_int64(GglBuffer str, int64_t *value);

#endif
