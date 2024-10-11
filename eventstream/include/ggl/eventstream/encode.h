// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_EVENTSTREAM_H
#define GGL_EVENTSTREAM_H

//! AWS EventStream message encoding.

#include "types.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stddef.h>

/// Encode an EventStream packet into a buffer.
GglError eventstream_encode(
    GglBuffer *buf,
    const EventStreamHeader *headers,
    size_t header_count,
    GglError (*payload_writer)(GglBuffer *buf, void *payload),
    void *payload
);

#endif
