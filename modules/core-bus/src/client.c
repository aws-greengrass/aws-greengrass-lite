// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "client_common.h"
#include "object_serde.h"
#include "types.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/eventstream/decode.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/socket.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stddef.h>

GgError ggl_notify(GgBuffer interface, GgBuffer method, GgMap params) {
    int conn_fd = -1;
    GgError ret = ggl_client_send_message(
        interface, GGL_CORE_BUS_NOTIFY, method, params, &conn_fd
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    (void) gg_close(conn_fd);
    return GG_ERR_OK;
}

GgError ggl_call(
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GgError *error,
    GgArena *alloc,
    GgObject *result
) {
    int conn = -1;
    GgError ret = ggl_client_send_message(
        interface, GGL_CORE_BUS_CALL, method, params, &conn
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, conn);

    GG_MTX_SCOPE_GUARD(&ggl_core_bus_client_payload_array_mtx);

    GgBuffer recv_buffer = GG_BUF(ggl_core_bus_client_payload_array);
    EventStreamMessage msg = { 0 };
    GG_LOGT(
        "Waiting for response from %.*s.", (int) interface.len, interface.data
    );
    ret = ggl_client_get_response(
        gg_socket_reader(&conn), recv_buffer, error, &msg
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (result != NULL) {
        ret = ggl_deserialize(alloc, msg.payload, result);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to decode response payload.");
            return ret;
        }

        ret = gg_arena_claim_obj(result, alloc);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Insufficient memory to return response payload.");
            return ret;
        }
    }

    return GG_ERR_OK;
}
