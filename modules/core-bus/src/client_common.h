// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_BUS_CLIENT_COMMON_H
#define CORE_BUS_CLIENT_COMMON_H

#include "types.h"
#include <sys/types.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/constants.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/io.h>
#include <ggl/object.h>
#include <stdint.h>

extern uint8_t ggl_core_bus_client_payload_array[GGL_COREBUS_MAX_MSG_LEN];
extern pthread_mutex_t ggl_core_bus_client_payload_array_mtx;

GglError ggl_client_send_message(
    GglBuffer interface,
    GglCoreBusRequestType type,
    GglBuffer method,
    GglMap params,
    int *conn_fd
);

GglError ggl_client_get_response(
    GglReader reader,
    GglBuffer recv_buffer,
    GglError *error,
    EventStreamMessage *response
);

#endif
