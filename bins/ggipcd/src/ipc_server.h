// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVER_H
#define GGL_IPC_SERVER_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

/// Maximum size of eventstream packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

/// Start the GG-IPC server on a given socket
GglError ggl_ipc_listen(const char *socket_name, const char *socket_path);

/// Send an EventStream packet to an IPC client.
GglError ggl_ipc_response_send(
    uint32_t handle,
    int32_t stream_id,
    GglBuffer service_model_type,
    GglObject response
);

/// Get the component name associated with a client.
/// component_name is an out parameter only.
GglError ggl_ipc_get_component_name(uint32_t handle, GglBuffer *component_name);

#endif
