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
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Inhibit mechanism for the first callback during subscription setup.
// Serializes correctly because ggipcd dispatches at most one subscribe of this
// operation at a time; steady-state callbacks bypass via handle+stream compare.
//
// All fields below are guarded by setup_mtx.  Sends happen under the mutex so
// that event ordering on the wire matches the true status ordering.
static pthread_mutex_t setup_mtx = PTHREAD_MUTEX_INITIALIZER;
static bool setup_in_progress = false;
static uint32_t in_progress_handle;
static int32_t in_progress_stream;
static int deferred_status = -1; // -1 none, 0 disconnected, 1 connected

/// Wire format: {"connectionStatusEvent": {"status":
/// "CONNECTED"|"DISCONNECTED"}}
static GgError send_status_event(
    uint32_t handle, int32_t stream_id, bool connected
) {
    GgBuffer status_str
        = connected ? GG_STR("CONNECTED") : GG_STR("DISCONNECTED");

    GgMap response = GG_MAP(gg_kv(
        GG_STR("connectionStatusEvent"),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("status"), gg_obj_buf(status_str))))
    ));

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#IoTCoreConnectionStatusEvent"),
        response
    );
}

static GgError connection_status_callback(
    void *ctx,
    GgObject data,
    uint32_t resp_handle,
    int32_t stream_id,
    GgArena *alloc
) {
    (void) ctx;
    (void) alloc;

    if (gg_obj_type(data) != GG_TYPE_BOOLEAN) {
        GG_LOGE("Unexpected non-boolean connection status object; skipping.");
        return GG_ERR_OK;
    }

    bool connected = gg_obj_into_bool(data);

    GgError ret;
    {
        GG_MTX_SCOPE_GUARD(&setup_mtx);

        if (setup_in_progress && (in_progress_stream == stream_id)
            && (in_progress_handle == resp_handle)) {
            // Handler is still setting up this stream; defer the event.
            deferred_status = connected ? 1 : 0;
            return GG_ERR_OK;
        }

        ret = send_status_event(resp_handle, stream_id, connected);
    }

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

    {
        GG_MTX_SCOPE_GUARD(&setup_mtx);
        setup_in_progress = true;
        in_progress_handle = handle;
        in_progress_stream = stream_id;
        deferred_status = -1;
    }

    GgError ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GG_STR("aws_iot_mqtt"),
        GG_STR("connection_status"),
        (GgMap) { 0 },
        connection_status_callback,
        NULL,
        NULL
    );
    if (ret != GG_ERR_OK) {
        {
            GG_MTX_SCOPE_GUARD(&setup_mtx);
            setup_in_progress = false;
            deferred_status = -1;
        }
        GG_LOGE("Failed to bind connection status subscription.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to bind connection status "
                              "subscription.") };
        return ret;
    }

    int s = -1;
    GgError send_ret = GG_ERR_OK;
    {
        GG_MTX_SCOPE_GUARD(&setup_mtx);

        ret = ggl_ipc_response_send(
            handle,
            stream_id,
            GG_STR("aws.greengrass#SubscribeToIoTCoreConnectionStatusResponse"),
            (GgMap) { 0 }
        );
        if (ret != GG_ERR_OK) {
            setup_in_progress = false;
            deferred_status = -1;
            GG_LOGW(
                "Failed to send subscription response (error %s).",
                gg_strerror(ret)
            );
            return ret;
        }

        s = deferred_status;
        deferred_status = -1;
        setup_in_progress = false;

        if (s >= 0) {
            send_ret = send_status_event(handle, stream_id, s == 1);
        }
    }

    if (s >= 0 && send_ret != GG_ERR_OK) {
        GG_LOGW(
            "Failed to send deferred connection status event (error %s).",
            gg_strerror(send_ret)
        );
    }

    return GG_ERR_OK;
}
