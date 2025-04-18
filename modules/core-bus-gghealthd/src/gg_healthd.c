// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/core_bus/gg_healthd.h"
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdint.h>

GglError ggl_gghealthd_retrieve_component_status(
    GglBuffer component, GglBuffer *component_status
) {
    static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(buffer));

    GglObject result;
    GglError method_error = GGL_ERR_OK;
    GglError call_error = ggl_call(
        GGL_STR("gg_health"),
        GGL_STR("get_status"),
        GGL_MAP({ GGL_STR("component_name"), ggl_obj_buf(component) }),
        &method_error,
        &balloc.alloc,
        &result
    );
    if (call_error != GGL_ERR_OK) {
        return call_error;
    }
    if (method_error != GGL_ERR_OK) {
        return method_error;
    }
    if (ggl_obj_type(result) != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }
    GglMap result_map = ggl_obj_into_map(result);

    GglObject *lifecycle_state_obj = NULL;
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
    GglBuffer lifecycle_state = ggl_obj_into_buf(*lifecycle_state_obj);

    memcpy(component_status->data, lifecycle_state.data, lifecycle_state.len);
    component_status->len = lifecycle_state.len;
    return GGL_ERR_OK;
}
