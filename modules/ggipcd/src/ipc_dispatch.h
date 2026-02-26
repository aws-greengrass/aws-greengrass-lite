// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_DISPATCH_H
#define GGL_IPC_DISPATCH_H

#include "ipc_error.h"
#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>

GgError ggl_ipc_handle_operation(
    GgBuffer operation,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error
);

#endif
