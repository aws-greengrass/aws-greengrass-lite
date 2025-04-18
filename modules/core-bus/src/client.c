// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/client.h"
#include "client_common.h"
#include "object_serde.h"
#include "types.h"
#include <ggl/arena.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/eventstream/decode.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <stddef.h>

GglError ggl_notify(GglBuffer interface, GglBuffer method, GglMap params) {
    int conn_fd = -1;
    GglError ret = ggl_client_send_message(
        interface, GGL_CORE_BUS_NOTIFY, method, params, &conn_fd
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    ggl_close(conn_fd);
    return GGL_ERR_OK;
}

GglError ggl_call(
    GglBuffer interface,
    GglBuffer method,
    GglMap params,
    GglError *error,
    GglArena *alloc,
    GglObject *result
) {
    int conn = -1;
    GglError ret = ggl_client_send_message(
        interface, GGL_CORE_BUS_CALL, method, params, &conn
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP(cleanup_close, conn);

    GGL_MTX_SCOPE_GUARD(&ggl_core_bus_client_payload_array_mtx);

    GglBuffer recv_buffer = GGL_BUF(ggl_core_bus_client_payload_array);
    EventStreamMessage msg = { 0 };
    GGL_LOGT(
        "Waiting for response from %.*s.", (int) interface.len, interface.data
    );
    ret = ggl_client_get_response(
        ggl_socket_reader(&conn), recv_buffer, error, &msg
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (result != NULL) {
        ret = ggl_deserialize(alloc, msg.payload, result);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to decode response payload.");
            return ret;
        }

        ret = ggl_arena_claim_obj(result, alloc);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Insufficient memory to return response payload.");
            return ret;
        }
    }

    return GGL_ERR_OK;
}
