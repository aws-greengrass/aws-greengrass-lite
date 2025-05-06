// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/gg_healthd.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdint.h>

GglError ggl_gghealthd_retrieve_component_status(
    GglBuffer component, GglArena *alloc, GglBuffer *component_status
) {
    static uint8_t resp_mem[256] = { 0 };
    GglArena resp_alloc = ggl_arena_init(GGL_BUF(resp_mem));

    GglObject result;
    GglError method_error;
    GglError ret = ggl_call(
        GGL_STR("gg_health"),
        GGL_STR("get_status"),
        GGL_MAP(ggl_kv(GGL_STR("component_name"), ggl_obj_buf(component))),
        &method_error,
        &resp_alloc,
        &result
    );
    if (ret != GGL_ERR_OK) {
        if (ret == GGL_ERR_REMOTE) {
            return method_error;
        }
        return ret;
    }
    if (ggl_obj_type(result) != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }
    GglMap result_map = ggl_obj_into_map(result);

    GglObject *lifecycle_state_obj;
    if (!ggl_map_get(
            result_map, GGL_STR("lifecycle_state"), &lifecycle_state_obj
        )) {
        GGL_LOGE(
            "Failed to retrieve lifecycle state of %.*s.",
            (int) component.len,
            component.data
        );
        return GGL_ERR_NOENTRY;
    }
    if (ggl_obj_type(*lifecycle_state_obj) != GGL_TYPE_BUF) {
        GGL_LOGE("Invalid response; lifecycle state must be a buffer.");
        return GGL_ERR_INVALID;
    }
    *component_status = ggl_obj_into_buf(*lifecycle_state_obj);

    ret = ggl_arena_claim_buf(component_status, alloc);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Insufficient memory to return lifecycle state.");
        return ret;
    }

    return GGL_ERR_OK;
}
