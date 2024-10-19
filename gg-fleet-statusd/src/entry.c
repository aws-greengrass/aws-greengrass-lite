// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "fleet_status_service.h"
#include "gg_fleet_statusd.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdint.h>

GglError run_gg_fleet_statusd(void) {
    GGL_LOGI("Started gg-fleet-statusd process.");

    static uint8_t thing_name_mem[MAX_THING_NAME_LEN] = { 0 };
    GglBuffer thing_name = GGL_BUF(thing_name_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read thingName from config.");
        return ret;
    }

    GglFleetStatusServiceThreadArgs args
        = { .thing_name = thing_name, .trigger = GGL_STR("NUCLEUS_LAUNCH") };

    // send an update on launch
    ret = publish_fleet_status_update(&args);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to publish fleet status update on launch.");
    }

    // update trigger for subsequent fss updates
    args.trigger = GGL_STR("CADENCE");

    pthread_t ptid_fss;
    pthread_create(&ptid_fss, NULL, &ggl_fleet_status_service_thread, &args);
    pthread_detach(ptid_fss);

    gg_fleet_statusd_start_server();

    return GGL_ERR_OK;
}
