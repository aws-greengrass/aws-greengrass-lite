// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iot_jobs_listener.h"
#include <ggl/error.h>
#include <ggl/object.h>
#include <ggl/bump_alloc.h>
#include <stdbool.h>

static GglError get_thing_name(char **thing_name) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("thingName")) }
    );

    static uint8_t resp_mem[128] = { 0 };
    GglBumpAlloc balloc
        = ggl_bump_alloc_init((GglBuffer) { .data = resp_mem, .len = 127 });

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
        GGL_LOGW("ggdeploymentd", "Failed to get thing name from config.");
        return ret;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration thing name is not a string.");
        return GGL_ERR_INVALID;
    }

    resp.buf.data[resp.buf.len] = '\0';
    *thing_name = (char *) resp.buf.data;
    return GGL_ERR_OK;
}

void listen_for_jobs_deployments(void) {
    char *thing_name = NULL;
    GglError ret = get_thing_name(&thing_name);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    while (true) {
        GglBuffer server = GGL_STR("/aws/ggl/iotcored");
        static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };

        GglMap args = GGL_MAP(
            { GGL_STR("topic_filter"),
            GGL_OBJ_STR("$aws/things/") }
        );

        GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(buffer));
        GglObject result;

        GglError ret = ggl_call(
            server,
            GGL_STR("subscribe"),
            args,
            NULL,
            &alloc.alloc,
            &result
        );

        if (ret != 0) {
            GGL_LOGE(
                "jobs-listener",
                "Failed to send subscribe to iotcored: %d.",
                ret
            );
            return EPROTO;
        }
    }
}
