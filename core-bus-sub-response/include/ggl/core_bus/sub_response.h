// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_SUB_RESPONSE_H
#define GGL_CORE_BUS_SUB_RESPONSE_H

//! core-bus-sub-response core-bus interface wrapper

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

typedef GglError (*GglSubResponseCallback)(void *ctx, GglObject data);

/// Wrapper for core-bus `ggl_subscribe`
/// Calls a callback function on the first subscription response, then
/// returns
GglError ggl_sub_response(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglSubResponseCallback callback,
    void *ctx,
    GglError *method_error,
    int64_t timeout_seconds
);

#endif
