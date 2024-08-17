// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include "gg_fleet_statusd.h"
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stddef.h>

static GglBuffer thing_name;

static GglError update_thing_name(void) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName")) }
    );

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("gg-fleet-statusd", "Failed to get thing name from config.");
        if ((ret == GGL_ERR_NOMEM) || (ret == GGL_ERR_FATAL)) {
            return ret;
        }
        return GGL_ERR_OK;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "gg-fleet-statusd", "Configuration thing name is not a string."
        );
        return GGL_ERR_INVALID;
    }

    thing_name = resp.buf;
    return GGL_ERR_OK;
}

GglError run_gg_fleet_statusd(void) {
    update_thing_name();
    publish_message(thing_name);
    return GGL_ERR_FAILURE;
}
