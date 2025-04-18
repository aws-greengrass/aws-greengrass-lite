// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_BUS_OBJECT_SERDE_H
#define CORE_BUS_OBJECT_SERDE_H

//! Serialization/Deserialization for GGL objects.

#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/io.h>
#include <ggl/object.h>

// TODO: serialize should take writer, deserialize should take reader

/// Serialize an object into a buffer.
GglError ggl_serialize(GglObject obj, GglBuffer *buf);

/// Deserialize an object from a buffer.
/// The resultant object holds references into the buffer.
GglError ggl_deserialize(GglArena *alloc, GglBuffer buf, GglObject *obj);

/// Reader from which a serialized object can be read.
/// Errors if buffer is not large enough for entire object.
GglReader ggl_serialize_reader(GglObject *obj);

#endif
