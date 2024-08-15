// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_EVENTSTREAM_RPC_H
#define GGL_EVENTSTREAM_RPC_H

//! AWS EventStream message data types.

#include "decode.h"
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// `:message-type` header values
typedef enum {
    EVENTSTREAM_APPLICATION_MESSAGE = 0,
    EVENTSTREAM_APPLICATION_ERROR = 1,
    EVENTSTREAM_CONNECT = 4,
    EVENTSTREAM_CONNECT_ACK = 5,
} EventStreamMessageType;

/// `:message-flags` header flags
typedef enum {
    EVENTSTREAM_CONNECTION_ACCEPTED = 1,
    EVENTSTREAM_TERMINATE_STREAM = 2,
} EventStreamMessageFlags;

static const int32_t EVENTSTREAM_FLAGS_MASK = 3;

typedef struct {
    int32_t stream_id;
    int32_t message_type;
    int32_t message_flags;
} EventStreamCommonHeaders;

/// Decode common EventStream headers
GglError eventstream_get_common_headers(
    EventStreamMessage *msg, EventStreamCommonHeaders *out
);

#endif
