// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include "gg_fleet_statusd.h"
#include <sys/types.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/utils.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError connection_status_callback(
    void *ctx, uint32_t handle, GglObject data
);
static void connection_status_close_callback(void *ctx, uint32_t handle);
static void gg_fleet_statusd_start_server(void);
static void *ggl_fleet_status_service_thread(void *ctx);

static GglBuffer thing_name = { 0 };

static GglBuffer connection_trigger = GGL_STR("NUCLEUS_LAUNCH");

GglError run_gg_fleet_statusd(void) {
    GGL_LOGI("Started gg-fleet-statusd process.");

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN] = { 0 };
    thing_name = GGL_BUF(thing_name_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read thingName from config.");
        return ret;
    }

    ret = ggl_aws_iot_mqtt_connection_status(
        connection_status_callback, connection_status_close_callback, NULL, NULL
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to subscribe to MQTT connection status.");
    }

    pthread_t ptid_fss;
    pthread_create(&ptid_fss, NULL, &ggl_fleet_status_service_thread, NULL);
    pthread_detach(ptid_fss);

    gg_fleet_statusd_start_server();

    return GGL_ERR_FAILURE;
}

static GglError connection_status_callback(
    void *ctx, uint32_t handle, GglObject data
) {
    (void) ctx;
    (void) handle;

    bool connected;
    GglError ret = ggl_aws_iot_mqtt_connection_status_parse(data, &connected);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (connected) {
        GGL_LOGD(
            "Sending %.*s fleet status update.",
            (int) connection_trigger.len,
            connection_trigger.data
        );
        ret = publish_fleet_status_update(
            thing_name, connection_trigger, GGL_MAP()
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to publish fleet status update.");
        }
        connection_trigger = GGL_STR("RECONNECT");
    }

    return GGL_ERR_OK;
}

static void connection_status_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    GGL_LOGE("Lost connection to iotcored.");
    // TODO: Add reconnects (on another thread or with timer
}

static void *ggl_fleet_status_service_thread(void *ctx) {
    (void) ctx;

    GGL_LOGD("Starting fleet status service thread.");

    while (true) {
        // thread will wait 24 hours before sending another update
        GglError ret = ggl_sleep(86400);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Fleet status service thread failed to sleep, exiting.");
            return NULL;
        }

        ret = publish_fleet_status_update(
            thing_name, GGL_STR("CADENCE"), GGL_MAP()
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to publish fleet status update.");
        }
    }

    return NULL;
}

static GglError send_fleet_status_update(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;
    GGL_LOGT("Received send_fleet_status_update from core bus.");

    GglObject *trigger = NULL;
    bool found = ggl_map_get(params, GGL_STR("trigger"), &trigger);
    if (!found || ggl_obj_type(*trigger) != GGL_TYPE_BUF) {
        GGL_LOGE("Missing required GGL_TYPE_BUF `trigger`.");
        return GGL_ERR_INVALID;
    }

    GglObject *deployment_info = NULL;
    found = ggl_map_get(params, GGL_STR("deployment_info"), &deployment_info);
    if (!found || ggl_obj_type(*deployment_info) != GGL_TYPE_MAP) {
        GGL_LOGE("Missing required GGL_TYPE_MAP `deployment_info`.");
        return GGL_ERR_INVALID;
    }

    GglError ret = publish_fleet_status_update(
        thing_name,
        ggl_obj_into_buf(*trigger),
        ggl_obj_into_map(*deployment_info)
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to publish fleet status update.");
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
