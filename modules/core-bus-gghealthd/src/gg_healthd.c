// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_healthd.h>
#include <stdint.h>

GgError ggl_gghealthd_retrieve_component_status(
    GgBuffer component, GgArena *alloc, GgBuffer *component_status
) {
    static uint8_t resp_mem[256] = { 0 };
    GgArena resp_alloc = gg_arena_init(GG_BUF(resp_mem));

    GgObject result;
    GgError method_error;
    GgError ret = ggl_call(
        GG_STR("gg_health"),
        GG_STR("get_status"),
        GG_MAP(gg_kv(GG_STR("component_name"), gg_obj_buf(component))),
        &method_error,
        &resp_alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        if (ret == GG_ERR_REMOTE) {
            return method_error;
        }
        return ret;
    }
    if (gg_obj_type(result) != GG_TYPE_MAP) {
        return GG_ERR_INVALID;
    }
    GgMap result_map = gg_obj_into_map(result);

    GgObject *lifecycle_state_obj;
    if (!gg_map_get(
            result_map, GG_STR("lifecycle_state"), &lifecycle_state_obj
        )) {
        GG_LOGE(
            "Failed to retrieve lifecycle state of %.*s.",
            (int) component.len,
            component.data
        );
        return GG_ERR_NOENTRY;
    }
    if (gg_obj_type(*lifecycle_state_obj) != GG_TYPE_BUF) {
        GG_LOGE("Invalid response; lifecycle state must be a buffer.");
        return GG_ERR_INVALID;
    }
    *component_status = gg_obj_into_buf(*lifecycle_state_obj);

    ret = gg_arena_claim_buf(component_status, alloc);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Insufficient memory to return lifecycle state.");
        return ret;
    }

    return GG_ERR_OK;
}
