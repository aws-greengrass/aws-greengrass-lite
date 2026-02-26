// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_BUS_OBJECT_SERDE_H
#define CORE_BUS_OBJECT_SERDE_H

//! Serialization/Deserialization for GGL objects.

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/io.h>
#include <gg/types.h>

// TODO: serialize should take writer, deserialize should take reader

/// Serialize an object into a buffer.
GgError ggl_serialize(GgObject obj, GgBuffer *buf);

/// Deserialize an object from a buffer.
/// The resultant object holds references into the buffer.
GgError ggl_deserialize(GgArena *alloc, GgBuffer buf, GgObject *obj);

/// Reader from which a serialized object can be read.
/// Errors if buffer is not large enough for entire object.
GgReader ggl_serialize_reader(GgObject *obj);

#endif
