// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVER_H
#define GGL_IPC_SERVER_H

#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

/// Maximum size of eventstream packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_IPC_MAX_MSG_LEN
#define GGL_IPC_MAX_MSG_LEN 10000
#endif

/// Start the GG-IPC server on a given socket
GgError ggl_ipc_listen(const GgBuffer *socket_name, GgBuffer socket_path);

/// Send an EventStream packet to an IPC client.
GgError ggl_ipc_response_send(
    uint32_t handle,
    int32_t stream_id,
    GgBuffer service_model_type,
    GgMap response
);

/// Get the component name associated with a client.
/// component_name is an out parameter only.
GgError ggl_ipc_get_component_name(uint32_t handle, GgBuffer *component_name);

#endif
