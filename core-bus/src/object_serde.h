// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_BUS_OBJECT_SERDE_H
#define CORE_BUS_OBJECT_SERDE_H

//! Serialization/Deserialization for GGL objects.

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdbool.h>

/// Serialize an object into a buffer.
GglError ggl_serialize(GglObject obj, GglBuffer *buf);

/// Deserialize an object from a buffer.
/// The resultant object holds references into the buffer, unless `copy_bufs` is
/// true, in which case all data will live in `alloc`.
GglError ggl_deserialize(
    GglAlloc *alloc, bool copy_bufs, GglBuffer buf, GglObject *obj
);

#endif
