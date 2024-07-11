/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EVENTSTREAM_DECODE_H
#define GGL_EVENTSTREAM_DECODE_H

/*! AWS EventStream message decoding. */

#include "eventstream_types.h"
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/** An iterator over EventStream headers. */
typedef struct {
    uint32_t count;
    uint8_t *pos;
} EventStreamHeaderIter;

/** A parsed EventStream packet. */
typedef struct {
    EventStreamHeaderIter headers;
    GglBuffer payload;
} EventStreamMessage;

/** Parse an EventStream packet from a buffer.
 * Parsed struct representation holds references into the buffer.
 * Validates headers. */
GglError eventstream_decode(GglBuffer buf, EventStreamMessage *msg);

/** Get the next header from an EventStreamHeaderIter.
 * Mutates the iter to refer to the rest of the headers.
 * Assumes headers already validated by decode. */
GglError eventstream_header_next(
    EventStreamHeaderIter *headers, EventStreamHeader *header
);

#endif
