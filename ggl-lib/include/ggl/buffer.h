// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BUFFER_H
#define GGL_BUFFER_H

//! Buffer utilities.

#include "object.h"
#include <ggl/error.h>
#include <stdbool.h>

/// Returns whether two buffers have identical content.
bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2);

/// Returns substring of buffer from start to end.
/// The result is the overlap between the start to end range and the input
/// bounds.
GglBuffer ggl_buffer_substr(GglBuffer buf, size_t start, size_t end);

/// Parse an integer from a string
GglError ggl_str_to_int64(GglBuffer str, int64_t *value);

#endif
