// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_EVENTSTREAM_DECODE_H
#define GGL_EVENTSTREAM_DECODE_H

//! AWS EventStream message decoding.

#include "types.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdint.h>

/// An iterator over EventStream headers.
typedef struct {
    uint32_t count;
    uint8_t *pos;
} EventStreamHeaderIter;

/// A parsed EventStream packet.
typedef struct {
    uint32_t data_len;
    uint32_t headers_len;
    uint32_t crc;
} EventStreamPrelude;

/// A parsed EventStream packet.
typedef struct {
    EventStreamHeaderIter headers;
    GglBuffer payload;
} EventStreamMessage;

/// Parse an EventStream packet prelude from a buffer.
GglError eventstream_decode_prelude(GglBuffer buf, EventStreamPrelude *prelude);

/// Parse an EventStream packet data section from a buffer.
/// The buffer should contain the rest of the packet after the prelude.
/// Parsed struct representation holds references into the buffer.
/// Validates headers.
GglError eventstream_decode(
    const EventStreamPrelude *prelude,
    GglBuffer data_section,
    EventStreamMessage *msg
);

/// Get the next header from an EventStreamHeaderIter.
/// Mutates the iter to refer to the rest of the headers.
/// Assumes headers already validated by decode.
GglError eventstream_header_next(
    EventStreamHeaderIter *headers, EventStreamHeader *header
);

#endif
