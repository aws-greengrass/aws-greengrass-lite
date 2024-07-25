/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_IPC_HANDLER_H
#define GGL_IPC_HANDLER_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError ggl_ipc_handle_operation(
    GglBuffer operation,
    GglMap args,
    GglAlloc *alloc,
    GglBuffer *service_model_type,
    GglObject *response
);

#endif
