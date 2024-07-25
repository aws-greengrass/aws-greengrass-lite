/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ipc_handler.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>

GglError ggl_ipc_handle_operation(
    GglBuffer operation,
    GglMap args,
    GglAlloc *alloc,
    GglBuffer *service_model_type,
    GglObject *response
) {
    struct {
        GglBuffer operation;
        GglError (*handler)(
            GglMap args,
            GglAlloc *alloc,
            GglBuffer *service_model_type,
            GglObject *response
        );
    } handler_table[] = {};

    size_t handler_count = sizeof(handler_table) / sizeof(handler_table[0]);

    for (size_t i = 0; i < handler_count; i++) {
        if (ggl_buffer_eq(operation, handler_table[i].operation)) {
            return handler_table[i].handler(
                args, alloc, service_model_type, response
            );
        }
    }

    GGL_LOGW("ipc-server", "Unhandled operation requested.");
    return GGL_ERR_NOENTRY;
}
