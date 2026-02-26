// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_CORE_BUS_SUB_RESPONSE_H
#define GGL_CORE_BUS_SUB_RESPONSE_H

//! core-bus-sub-response core-bus interface wrapper

#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

typedef GgError (*GglSubResponseCallback)(void *ctx, GgObject data);

/// Wrapper for core-bus `ggl_subscribe`
/// Calls a callback function on the first subscription response, then returns
GgError ggl_sub_response(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglSubResponseCallback callback,
    void *ctx,
    GgError *remote_error,
    int64_t timeout_seconds
);

#endif
