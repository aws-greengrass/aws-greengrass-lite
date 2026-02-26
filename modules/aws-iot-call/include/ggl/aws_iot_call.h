// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IOT_CORE_CALL_H
#define GGL_IOT_CORE_CALL_H

//! Helper for calling AWS IoT Core APIs

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

/// Make a call to an AWS IoT MQTT API.
/// Sends request on topic and waits for response on topic/(accepted|rejected).
/// Responses will be filtered according to clientToken.
GgError ggl_aws_iot_call(
    GgBuffer socket_name,
    GgBuffer topic,
    GgObject payload,
    bool virtual,
    GgArena *alloc,
    GgObject *result
);

#endif
