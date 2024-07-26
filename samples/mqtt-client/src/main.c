/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <stdlib.h>

static void subscribe_callback(
    void *ctx, GglSubscription subscription, GglObject data
) {
    (void) ctx;
    (void) subscription;

    if (data.type != GGL_TYPE_MAP) {
        GGL_LOGE("mqtt-client", "Subscription response is not a map.");
        return;
    }

    GglBuffer topic = GGL_STR("");
    GglBuffer payload = GGL_STR("");

    GglObject *val;
    if (ggl_map_get(data.map, GGL_STR("topic"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "mqtt-client", "Subscription response topic not a buffer."
            );
            return;
        }
        topic = val->buf;
    } else {
        GGL_LOGE("mqtt-client", "Subscription response is missing topic.");
        return;
    }
    if (ggl_map_get(data.map, GGL_STR("payload"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE(
                "mqtt-client", "Subscription response payload not a buffer."
            );
            return;
        }
        payload = val->buf;
    } else {
        GGL_LOGE("mqtt-client", "Subscription response is missing payload.");
        return;
    }

    GGL_LOGI(
        "mqtt-client",
        "Got message from IoT Core; topic: %.*s, payload: %.*s.",
        (int) topic.len,
        topic.data,
        (int) payload.len,
        payload.data
    );
}

int main(void) {
    GglBuffer iotcored = GGL_STR("/aws/ggl/iotcored");

    GglMap subscribe_args
        = GGL_MAP({ GGL_STR("topic_filter"), GGL_OBJ_STR("hello") }, );

    GglError ret = ggl_subscribe(
        iotcored,
        GGL_STR("subscribe"),
        subscribe_args,
        subscribe_callback,
        NULL,
        NULL,
        NULL,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "mqtt-client",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }
    GGL_LOGI("mqtt-client", "Successfully sent subscription.");

    ggl_sleep(1);

    GglMap publish_args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ_STR("hello") },
        { GGL_STR("payload"), GGL_OBJ_STR("hello world") },
    );

    ret = ggl_notify(iotcored, GGL_STR("publish"), publish_args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "mqtt-client",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }
    GGL_LOGI("mqtt-client", "Sent MQTT publish.");

    ggl_sleep(5);
}
