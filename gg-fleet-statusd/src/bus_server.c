// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "fleet_status_service.h"
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdint.h>

static void send_fleet_status_update(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;
    GGL_LOGT("Received send_fleet_status_update from core bus.");

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN] = { 0 };
    GglBuffer thing_name = GGL_BUF(thing_name_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read thingName from config.");
        ggl_return_err(handle, ret);
        return;
    }

    GglObject *trigger = NULL;
    bool found = ggl_map_get(params, GGL_STR("trigger"), &trigger);
    if (!found || trigger->type != GGL_TYPE_BUF) {
        GGL_LOGE("Missing required GGL_TYPE_BUF `trigger`.");
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    GglFleetStatusServiceThreadArgs args = {.thing_name = thing_name, .trigger = trigger->buf};

    ret = publish_fleet_status_update(&args);
    if(ret != GGL_ERR_OK) {
      ggl_return_err(handle, ret);
    }
}

void gg_fleet_statusd_start_server(void) {
    GGL_LOGI("Starting gg-fleet-statusd core bus server.");

    GglRpcMethodDesc handlers[] = { { GGL_STR("send_fleet_status_update"),
                                      false,
                                      send_fleet_status_update,
                                      NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret = ggl_listen(
        GGL_STR("/aws/ggl/gg-fleet-statusd"), handlers, handlers_len
    );

    GGL_LOGE("Exiting with error %u.", (unsigned) ret);
}
