// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_handler.h"
#include "ipc_server.h"
#include <ggl/alloc.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static uint8_t core_bus_resp_mem
    [(GGL_IPC_PAYLOAD_MAX_SUBOBJECTS * sizeof(GglObject))
     + GGL_IPC_MAX_MSG_LEN];
static pthread_mutex_t core_bus_resp_mem_mtx = PTHREAD_MUTEX_INITIALIZER;

static void subscription_on_close(void *ctx, uint32_t handle) {
    GglIpcSubscriptionCtx *sub_ctx = ctx;
    (void) handle;
    ggl_ipc_release_subscription_ctx(sub_ctx);
}

static GglError handle_publish_to_iot_core(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    GglBuffer topic;
    GglBuffer payload;
    int64_t qos;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("topicName"), &val);
    if (!found) {
        GGL_LOGE("PublishToIoTCore", "Missing topicName.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("PublishToIoTCore", "topicName not a string.");
        return GGL_ERR_INVALID;
    }
    topic = val->buf;

    found = ggl_map_get(args, GGL_STR("payload"), &val);
    if (!found) {
        payload = GGL_STR("");
    } else {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("PublishToIoTCore", "topicName not a string.");
            return GGL_ERR_INVALID;
        }
        payload = val->buf;
    }

    found = ggl_map_get(args, GGL_STR("qos"), &val);
    if (!found) {
        qos = 0;
    } else {
        if (val->type == GGL_TYPE_I64) {
            qos = val->i64;
        } else if (val->type == GGL_TYPE_BUF) {
            GglError ret = ggl_str_to_int64(val->buf, &qos);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "PublishToIoTCore", "Failed to parse qos string value."
                );
                return ret;
            }
        } else {
            GGL_LOGE("PublishToIoTCore", "qos not an valid type.");
            return GGL_ERR_INVALID;
        }
    }

    bool decoded = ggl_base64_decode_in_place(&payload);
    if (!decoded) {
        GGL_LOGE("PublishToIoTCore", "payload is not valid base64.");
        return GGL_ERR_INVALID;
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ(topic) },
        { GGL_STR("payload"), GGL_OBJ(payload) },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) },
    );

    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/iotcored"),
        GGL_STR("publish"),
        call_args,
        NULL,
        alloc,
        &call_resp
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#PublishToIoTCoreResponse"),
        GGL_OBJ_MAP()
    );
}

static GglError subscribe_to_iot_core_callback(
    void *ctx, uint32_t recv_handle, GglObject data
) {
    GglIpcSubscriptionCtx *sub_ctx = ctx;
    (void) recv_handle;

    GglBuffer topic;
    GglBuffer payload;

    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("SubscribeToIoTCore", "Subscription response not a map.");
        return GGL_ERR_FAILURE;
    }

    GglObject *val = NULL;
    bool found = ggl_map_get(data.map, GGL_STR("topic"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToIoTCore", "Subscription response missing topic.");
        return GGL_ERR_FAILURE;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "SubscribeToIoTCore", "Subscription response topic not a buffer."
        );
        return GGL_ERR_INVALID;
    }
    topic = val->buf;

    found = ggl_map_get(data.map, GGL_STR("payload"), &val);
    if (!found) {
        GGL_LOGE(
            "SubscribeToIoTCore", "Subscription response missing payload."
        );
        return GGL_ERR_FAILURE;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "SubscribeToIoTCore", "Subscription response payload not a buffer."
        );
        return GGL_ERR_INVALID;
    }
    payload = val->buf;

    pthread_mutex_lock(&core_bus_resp_mem_mtx);
    GGL_DEFER(pthread_mutex_unlock, core_bus_resp_mem_mtx);
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(core_bus_resp_mem));

    GglBuffer base64_payload;
    GglError ret = ggl_base64_encode(payload, &balloc.alloc, &base64_payload);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "SubscribeToIoTCore",
            "Insufficent memory to base64 encode payload; skipping."
        );
        return GGL_ERR_OK;
    }

    GglObject response
        = GGL_OBJ_MAP({ GGL_STR("message"),
                        GGL_OBJ_MAP(
                            { GGL_STR("topicName"), GGL_OBJ(topic) },
                            { GGL_STR("payload"), GGL_OBJ(base64_payload) }
                        ) });

    ret = ggl_ipc_response_send(
        sub_ctx->resp_handle,
        sub_ctx->stream_id,
        GGL_STR("aws.greengrass#IoTCoreMessage"),
        response
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "SubscribeToIoTCore",
            "Failed to send subscription response; skipping."
        );
        return GGL_ERR_OK;
    }

    return GGL_ERR_OK;
}

static GglError handle_subscribe_to_iot_core(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;
    GglBuffer topic_filter;
    int64_t qos;

    GglObject *val = NULL;
    bool found = ggl_map_get(args, GGL_STR("topicName"), &val);
    if (!found) {
        GGL_LOGE("SubscribeToIoTCore", "Missing topicName.");
        return GGL_ERR_INVALID;
    }
    if (val->type != GGL_TYPE_BUF) {
        GGL_LOGE("SubscribeToIoTCore", "topicName not a string.");
        return GGL_ERR_INVALID;
    }
    topic_filter = val->buf;

    found = ggl_map_get(args, GGL_STR("qos"), &val);
    if (!found) {
        qos = 0;
    } else {
        if (val->type == GGL_TYPE_I64) {
            qos = val->i64;
        } else if (val->type == GGL_TYPE_BUF) {
            GglError ret = ggl_str_to_int64(val->buf, &qos);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "SubscribeToIoTCore", "Failed to parse qos string value."
                );
                return ret;
            }
        } else {
            GGL_LOGE("SubscribeToIoTCore", "qos not an valid type.");
            return GGL_ERR_INVALID;
        }
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("topic_filter"), GGL_OBJ(topic_filter) },
        { GGL_STR("qos"), GGL_OBJ_I64(qos) },
    );

    GglIpcSubscriptionCtx *ctx = NULL;
    GglError ret = ggl_ipc_get_subscription_ctx(&ctx, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ctx->stream_id = stream_id;

    uint32_t recv_handle = 0;
    ret = ggl_subscribe(
        GGL_STR("/aws/ggl/iotcored"),
        GGL_STR("subscribe"),
        call_args,
        subscribe_to_iot_core_callback,
        subscription_on_close,
        ctx,
        NULL,
        &recv_handle
    );
    if (ret != GGL_ERR_OK) {
        ggl_ipc_release_subscription_ctx(ctx);
        return ret;
    }

    ctx->recv_handle = recv_handle;

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToIoTCoreResponse"),
        GGL_OBJ_MAP()
    );
}

GglError ggl_ipc_handle_operation(
    GglBuffer operation, GglMap args, uint32_t handle, int32_t stream_id
) {
    struct {
        GglBuffer operation;
        GglError (*handler)(
            GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
        );
    } handler_table[] = {
        { GGL_STR("aws.greengrass#PublishToIoTCore"),
          handle_publish_to_iot_core },
        { GGL_STR("aws.greengrass#SubscribeToIoTCore"),
          handle_subscribe_to_iot_core },
    };

    size_t handler_count = sizeof(handler_table) / sizeof(handler_table[0]);

    for (size_t i = 0; i < handler_count; i++) {
        if (ggl_buffer_eq(operation, handler_table[i].operation)) {
            pthread_mutex_lock(&core_bus_resp_mem_mtx);
            GGL_DEFER(pthread_mutex_unlock, core_bus_resp_mem_mtx);
            GglBumpAlloc balloc
                = ggl_bump_alloc_init(GGL_BUF(core_bus_resp_mem));

            return handler_table[i].handler(
                args, handle, stream_id, &balloc.alloc
            );
        }
    }

    GGL_LOGW("ipc-server", "Unhandled operation requested.");
    return GGL_ERR_NOENTRY;
}
