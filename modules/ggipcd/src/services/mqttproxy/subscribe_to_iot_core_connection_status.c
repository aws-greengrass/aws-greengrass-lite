// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "mqttproxy.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Translates core-bus boolean connection status into the IPC stream event.
/// Wire format: {"connectionStatusEvent": {"status":
/// "CONNECTED"|"DISCONNECTED"}}
static GgError connection_status_callback(
    GgObject data, uint32_t resp_handle, int32_t stream_id, GgArena *alloc
) {
    (void) alloc;

    if (gg_obj_type(data) != GG_TYPE_BOOLEAN) {
        GG_LOGE("Unexpected non-boolean connection status object; skipping.");
        return GG_ERR_OK;
    }

    bool connected = gg_obj_into_bool(data);

    GgBuffer status_str
        = connected ? GG_STR("CONNECTED") : GG_STR("DISCONNECTED");

    GgMap response = GG_MAP(gg_kv(
        GG_STR("connectionStatusEvent"),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("status"), gg_obj_buf(status_str))))
    ));

    GgError ret = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GG_STR("aws.greengrass#IoTCoreConnectionStatusEvent"),
        response
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to send connection status event (error %s); "
            "closing subscription.",
            gg_strerror(ret)
        );
        return ret;
    }

    return GG_ERR_OK;
}

GgError ggl_handle_subscribe_to_iot_core_connection_status(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) info;
    (void) args;
    (void) alloc;

    // No authorization check: connection status is informational and
    // local-only.

    // Send the operation response BEFORE binding the core-bus subscription.
    // iotcored publishes the current connection status immediately upon
    // subscription accept, on a separate core-bus subscription thread.
    // If we bound first, we could:
    //   (a) violate the eventstream protocol by emitting a stream event
    //       before the operation response, or
    //   (b) lose the first event due to the recv_handle registration gap
    //       inside ggl_ipc_bind_subscription.
    GgError ret = ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#SubscribeToIoTCoreConnectionStatusResponse"),
        (GgMap) { 0 }
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to send subscription response.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to send subscription response.") };
        return ret;
    }

    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GG_STR("aws_iot_mqtt"),
        GG_STR("connection_status"),
        (GgMap) { 0 },
        connection_status_callback,
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to bind connection status subscription after response "
                "sent; terminating stream.");
        ggl_ipc_terminate_stream(handle, stream_id);
        // Return GG_ERR_OK because the success response was already sent.
        // Returning non-OK here would cause handle_operation (ipc_server.c)
        // to call send_stream_error, sending a second error frame on an
        // already-responded stream.
        return GG_ERR_OK;
    }

    return GG_ERR_OK;
}
