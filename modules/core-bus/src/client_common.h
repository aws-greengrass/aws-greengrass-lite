// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CORE_BUS_CLIENT_COMMON_H
#define CORE_BUS_CLIENT_COMMON_H

#include "types.h"
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/io.h>
#include <gg/types.h>
#include <ggl/core_bus/constants.h>
#include <sys/types.h>
#include <stdint.h>

extern uint8_t ggl_core_bus_client_payload_array[GGL_COREBUS_MAX_MSG_LEN];
extern pthread_mutex_t ggl_core_bus_client_payload_array_mtx;

GgError ggl_client_send_message(
    GgBuffer interface,
    GglCoreBusRequestType type,
    GgBuffer method,
    GgMap params,
    int *conn_fd
);

GgError ggl_client_get_response(
    GgReader reader,
    GgBuffer recv_buffer,
    GgError *error,
    EventStreamMessage *response
);

#endif
