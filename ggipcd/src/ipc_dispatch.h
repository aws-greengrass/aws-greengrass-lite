// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_DISPATCH_H
#define GGL_IPC_DISPATCH_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError ggl_ipc_handle_operation(
    GglBuffer operation, GglMap args, uint32_t handle, int32_t stream_id
);

#endif
