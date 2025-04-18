// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_JSON_DECODE_H
#define GGL_JSON_DECODE_H

//! JSON decoding

#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

/// Reads a JSON doc from a buffer as a GglObject.
/// Result obj may contain references into buf, and allocations from alloc.
/// Input buffer will be modified.
GglError ggl_json_decode_destructive(
    GglBuffer buf, GglArena *arena, GglObject *obj
);

#endif
