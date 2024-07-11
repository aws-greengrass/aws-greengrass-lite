/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/client.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <errno.h>

int main(void) {
    GglBuffer iotcored = GGL_STR("/aws/ggl/iotcored");

    GglMap args = GGL_MAP(
        { GGL_STR("topic"), GGL_OBJ_STR("hello") },
        { GGL_STR("payload"), GGL_OBJ_STR("hello world") },
    );

    GglError ret = ggl_notify(iotcored, GGL_STR("publish"), args);
    if (ret != 0) {
        GGL_LOGE(
            "mqtt-client",
            "Failed to send notify message to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EPROTO;
    }

    GGL_LOGI("mqtt-client", "Sent MQTT publish.");
}
