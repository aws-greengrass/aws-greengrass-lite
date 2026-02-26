// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_YAML_DECODE_H
#define GGL_YAML_DECODE_H

//! YAML decoding

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>

/// Reads a YAML doc from a buffer as a GgObject.
/// Result obj will contain pointers into both arena and buf.
/// buf value is overwritten.
GgError ggl_yaml_decode_destructive(
    GgBuffer buf, GgArena *arena, GgObject *obj
);

#endif
