// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include <ctype.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/utils.h>
#include <gg_fleet_statusd.h>
#include <ggl/core_bus/aws_iot_mqtt.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/core_bus/server.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GgError connection_status_callback(
    void *ctx, uint32_t handle, GgObject data
);
static void connection_status_close_callback(void *ctx, uint32_t handle);
static void gg_fleet_statusd_start_server(void);
static void *ggl_fleet_status_service_thread(void *ctx);
static uint64_t get_periodic_status_interval(void);
static bool parse_positive_integer(GgBuffer str, uint32_t *result);
static GgError init_fleet_status_service_config(void);

static GgBuffer thing_name = { 0 };

static GgBuffer connection_trigger = GG_STR("NUCLEUS_LAUNCH");

GgError run_gg_fleet_statusd(void) {
    GG_LOGI("Started gg-fleet-statusd process.");

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(thing_name_mem));

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")), &alloc, &thing_name
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read thingName from config.");
        return ret;
    }

    ret = ggl_aws_iot_mqtt_connection_status(
        GG_STR("aws_iot_mqtt"),
        connection_status_callback,
        connection_status_close_callback,
        NULL,
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to subscribe to MQTT connection status.");
    }

    ret = init_fleet_status_service_config();
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to initialize FleetStatusService configuration.");
        return ret;
    }

    pthread_t ptid_fss;
    pthread_create(&ptid_fss, NULL, &ggl_fleet_status_service_thread, NULL);
    pthread_detach(ptid_fss);

    gg_fleet_statusd_start_server();

    return GG_ERR_FAILURE;
}

static GgError connection_status_callback(
    void *ctx, uint32_t handle, GgObject data
) {
    (void) ctx;
    (void) handle;

    bool connected;
    GgError ret = ggl_aws_iot_mqtt_connection_status_parse(data, &connected);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (connected) {
        GG_LOGD(
            "Sending %.*s fleet status update.",
            (int) connection_trigger.len,
            connection_trigger.data
        );
        ret = publish_fleet_status_update(
            thing_name, connection_trigger, GG_MAP()
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to publish fleet status update.");
        }
        connection_trigger = GG_STR("RECONNECT");
    }

    return GG_ERR_OK;
}

static void connection_status_close_callback(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    GG_LOGE("Lost connection to iotcored.");
    // TODO: Add reconnects (on another thread or with timer
}

static bool parse_positive_integer(GgBuffer str, uint32_t *result) {
    if (str.len == 0) {
        return false;
    }

    size_t counter = 0;
    while (counter < str.len && isspace(str.data[counter])) {
        counter++;
    }

    *result = 0;
    for (size_t i = counter; i < str.len; i++) {
        if (!isdigit(str.data[i])) {
            return false;
        }
        uint32_t digit = str.data[i] - '0';
        if (*result > (UINT32_MAX - digit) / 10) {
            GG_LOGE("Integer overflow detected while parsing config value");
            return false;
        }
        *result = *result * 10 + digit;
    }

    return true;
}

static uint64_t get_periodic_status_interval(void) {
    static uint8_t config_mem[32] = { 0 };
    GgArena alloc = gg_arena_init(GG_BUF(config_mem));
    GgObject interval_obj;

    GgError ret = ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("fleetStatus"),
            GG_STR("periodicStatusPublishIntervalSeconds")
        ),
        &alloc,
        &interval_obj
    );
    GG_LOGD("Config read result: %d", ret);

    if (ret == GG_ERR_OK) {
        GG_LOGD("interval_obj type: %d", gg_obj_type(interval_obj));
        uint64_t interval_seconds = 0;

        if (gg_obj_type(interval_obj) == GG_TYPE_I64) {
            interval_seconds = (uint64_t) gg_obj_into_i64(interval_obj);
            GG_LOGD(
                "Found interval_obj value (int64): %" PRIu64, interval_seconds
            );
        } else if (gg_obj_type(interval_obj) == GG_TYPE_BUF) {
            uint32_t parsed_value;
            if (parse_positive_integer(
                    gg_obj_into_buf(interval_obj), &parsed_value
                )) {
                interval_seconds = parsed_value;
            } else {
                GG_LOGD(
                    "Invalid value. Using default periodic status interval: 86400 seconds"
                );
                return 86400; // Default 24 hours
            }
        }
        if (interval_seconds > 0) {
            GG_LOGD(
                "Using periodic status interval from config: %" PRIu64
                " seconds",
                interval_seconds
            );
            return interval_seconds;
        }
    }

    GG_LOGD("Using default periodic status interval: 86400 seconds");
    return 86400; // Default 24 hours
}

static void *ggl_fleet_status_service_thread(void *ctx) {
    (void) ctx;

    GG_LOGD("Starting fleet status service thread.");

    while (true) {
        uint64_t interval_seconds = get_periodic_status_interval();

        GgError ret = gg_sleep((uint32_t) interval_seconds);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Fleet status service thread failed to sleep, exiting.");
            return NULL;
        }

        ret = publish_fleet_status_update(
            thing_name, GG_STR("CADENCE"), GG_MAP()
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to publish fleet status update.");
        }
    }

    return NULL;
}

static GgError send_fleet_status_update(
    void *ctx, GgMap params, uint32_t handle
) {
    (void) ctx;
    GG_LOGT("Received send_fleet_status_update from core bus.");

    GgObject *trigger = NULL;
    bool found = gg_map_get(params, GG_STR("trigger"), &trigger);
    if (!found || gg_obj_type(*trigger) != GG_TYPE_BUF) {
        GG_LOGE("Missing required GG_TYPE_BUF `trigger`.");
        return GG_ERR_INVALID;
    }

    GgObject *deployment_info = NULL;
    found = gg_map_get(params, GG_STR("deployment_info"), &deployment_info);
    if (!found || gg_obj_type(*deployment_info) != GG_TYPE_MAP) {
        GG_LOGE("Missing required GG_TYPE_MAP `deployment_info`.");
        return GG_ERR_INVALID;
    }

    GgError ret = publish_fleet_status_update(
        thing_name, gg_obj_into_buf(*trigger), gg_obj_into_map(*deployment_info)
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to publish fleet status update.");
        return ret;
    }

    ggl_respond(handle, GG_OBJ_NULL);
    return GG_ERR_OK;
}

void gg_fleet_statusd_start_server(void) {
    GG_LOGI("Starting gg-fleet-statusd core bus server.");

    GglRpcMethodDesc handlers[] = { { GG_STR("send_fleet_status_update"),
                                      false,
                                      send_fleet_status_update,
                                      NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GgError ret = ggl_listen(GG_STR("gg_fleet_status"), handlers, handlers_len);

    GG_LOGE("Exiting with error %u.", (unsigned) ret);
}

static GgError init_fleet_status_service_config(void) {
    GgError ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"), GG_STR("FleetStatusService"), GG_STR("version")
        ),
        gg_obj_buf(GG_STR(GGL_VERSION)),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write FleetStatusService version to config.");
        return ret;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("FleetStatusService"),
            GG_STR("configArn")
        ),
        gg_obj_list(GG_LIST()),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write FleetStatusService configArn to config.");
        return ret;
    }

    return GG_ERR_OK;
}
