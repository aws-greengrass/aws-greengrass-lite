// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BASE64_H
#define GGL_BASE64_H

//! Base64 utilities

#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdbool.h>

/// Convert a base64 buffer to its decoded data.
bool ggl_base64_decode(GglBuffer base64, GglBuffer *target);

/// Convert a base64 buffer to its decoded data in place.
bool ggl_base64_decode_in_place(GglBuffer *target);

/// Encode a buffer into base64.
GglError ggl_base64_encode(GglBuffer buf, GglArena *alloc, GglBuffer *result);

#endif
