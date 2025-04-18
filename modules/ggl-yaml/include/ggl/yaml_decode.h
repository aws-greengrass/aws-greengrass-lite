// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_YAML_DECODE_H
#define GGL_YAML_DECODE_H

//! YAML decoding

#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

/// Reads a YAML doc from a buffer as a GglObject.
/// Result obj will contain pointers into both arena and buf.
/// buf value is overwritten.
GglError ggl_yaml_decode_destructive(
    GglBuffer buf, GglArena *arena, GglObject *obj
);

#endif
