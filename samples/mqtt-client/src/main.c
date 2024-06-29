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

    GglConn *conn;
    GglError ret = ggl_connect(iotcored, &conn);
    if (ret != 0) {
        GGL_LOGE(
            "mqtt-client",
            "Failed to connect to %.*s",
            (int) iotcored.len,
            iotcored.data
        );
        return EHOSTUNREACH;
    }

    GglList args = GGL_LIST(GGL_OBJ_MAP(
        { GGL_STR("topic"), GGL_OBJ_STR("hello") },
        { GGL_STR("payload"), GGL_OBJ_STR("hello world") },
    ));

    ggl_notify(conn, GGL_STR("publish"), args);

    GGL_LOGI("mqtt-client", "Sent MQTT publish.");
}
