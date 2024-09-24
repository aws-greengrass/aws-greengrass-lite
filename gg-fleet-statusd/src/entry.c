// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include "gg_fleet_statusd.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stdint.h>

#define MAX_THING_NAME_LEN 128

GglError run_gg_fleet_statusd(void) {
    static uint8_t thing_name_mem[MAX_THING_NAME_LEN] = { 0 };
    GglBuffer thing_name = GGL_BUF(thing_name_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("thingName")), &thing_name
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("fleet_status", "Failed to read thingName from config.");
        return ret;
    }

    return publish_message(thing_name);
}
