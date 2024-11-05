// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "fleet_status_service.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError send_fleet_status_update(
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
        return ret;
    }

    GglObject *trigger = NULL;
    bool found = ggl_map_get(params, GGL_STR("trigger"), &trigger);
    if (!found || trigger->type != GGL_TYPE_BUF) {
        GGL_LOGE("Missing required GGL_TYPE_BUF `trigger`.");
        return GGL_ERR_INVALID;
    }

    GglFleetStatusServiceThreadArgs args
        = { .thing_name = thing_name, .trigger = trigger->buf };

    ret = publish_fleet_status_update(&args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, GGL_OBJ_NULL());
    return GGL_ERR_OK;
}

void gg_fleet_statusd_start_server(void) {
    GGL_LOGI("Starting gg-fleet-statusd core bus server.");

    GglRpcMethodDesc handlers[] = { { GGL_STR("send_fleet_status_update"),
                                      false,
                                      send_fleet_status_update,
                                      NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret
        = ggl_listen(GGL_STR("gg_fleet_status"), handlers, handlers_len);

    GGL_LOGE("Exiting with error %u.", (unsigned) ret);
}
