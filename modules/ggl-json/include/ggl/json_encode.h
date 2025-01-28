// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_JSON_ENCODE_H
#define GGL_JSON_ENCODE_H

//! JSON encoding

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/io.h>
#include <ggl/object.h>

/// Serializes a GglObject into a buffer in JSON encoding.
GglError ggl_json_encode(GglObject obj, GglBuffer *buf);

/// Reader from which a JSON-serialized object can be read.
/// Errors if buffer is not large enough for entire object.
GglReader ggl_json_reader(GglObject *obj);

#endif
