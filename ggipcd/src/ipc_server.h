// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVER_H
#define GGL_IPC_SERVER_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// Maximum size of eventstream packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

#define GGL_IPC_PAYLOAD_MAX_SUBOBJECTS 50

GglError ggl_ipc_listen(const char *socket_path);

GglError ggl_ipc_response_send(
    uint32_t handle,
    int32_t stream_id,
    GglBuffer service_model_type,
    GglObject response
);

/// Callback for whenever a subscription is closed.
typedef GglError (*GglIpcSubscribeCallback)(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglAlloc *alloc
);

/// Wrapper around ggl_subscribe for IPC handlers.
GglError ggl_ipc_bind_subscription(
    uint32_t resp_handle,
    int32_t stream_id,
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglIpcSubscribeCallback on_response,
    GglError *error
) __attribute__((warn_unused_result));

#endif
