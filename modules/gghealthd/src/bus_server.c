// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "health.h"
#include "subscriptions.h"
#include <bus_server.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/server.h>
#include <ggl/nucleus/constants.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIFECYCLE_STATE_MAX_LEN (sizeof("INSTALLED") - 1U)

static GgError get_status(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;
    GgObject *component_name_obj;
    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA({ GG_STR("component_name"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &component_name_obj })
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("get_status received invalid arguments.");
        return GG_ERR_INVALID;
    }
    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("`component_name` too long");
        return GG_ERR_RANGE;
    }

    GgBuffer status = { 0 };
    GgError error = gghealthd_get_status(component_name, &status);
    if (error != GG_ERR_OK) {
        return error;
    }

    GG_LOGD(
        "%.*s is %.*s",
        (int) component_name.len,
        component_name.data,
        (int) status.len,
        status.data
    );
    ggl_respond(
        handle,
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("component_name"), gg_obj_buf(component_name)),
            gg_kv(GG_STR("lifecycle_state"), gg_obj_buf(status)),
        ))
    );
    return GG_ERR_OK;
}

static GgError update_status(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;
    GgObject *component_name_obj;
    GgObject *state_obj;
    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA(
            { GG_STR("component_name"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &component_name_obj },
            { GG_STR("lifecycle_state"), GG_REQUIRED, GG_TYPE_BUF, &state_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("update_status received invalid arguments.");
        return GG_ERR_INVALID;
    }
    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
    GgBuffer state = gg_obj_into_buf(*state_obj);

    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("`component_name` too long");
        return GG_ERR_RANGE;
    }
    if (state.len > LIFECYCLE_STATE_MAX_LEN) {
        GG_LOGE("`lifecycle_state` too long");
        return GG_ERR_RANGE;
    }

    GgError error = gghealthd_update_status(component_name, state);
    if (error != GG_ERR_OK) {
        return error;
    }

    ggl_respond(handle, GG_OBJ_NULL);
    return GG_ERR_OK;
}

static GgError get_health(void *ctx, GgMap params, uint32_t handle) {
    (void) params;
    (void) ctx;
    GgBuffer status = { 0 };
    GgError error = gghealthd_get_health(&status);

    if (error != GG_ERR_OK) {
        return error;
    }

    ggl_respond(handle, gg_obj_buf(status));
    return GG_ERR_OK;
}

// TODO: implement or remove this
static GgError subscribe_to_deployment_updates(
    void *ctx, GgMap params, uint32_t handle
) {
    (void) ctx;
    (void) params;
    (void) handle;
    return GG_ERR_UNSUPPORTED;
}

static GgError subscribe_to_lifecycle_completion(
    void *ctx, GgMap params, uint32_t handle
) {
    (void) ctx;
    GgObject *component_name_obj = NULL;
    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA({ GG_STR("component_name"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &component_name_obj })
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("subscribe_to_lifecycle_completion received invalid arguments."
        );
        return GG_ERR_INVALID;
    }
    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("`component_name` too long");
        return GG_ERR_RANGE;
    }

    ret = gghealthd_register_lifecycle_subscription(component_name, handle);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgBuffer status;
    GgError error = gghealthd_get_status(component_name, &status);
    if (error != GG_ERR_OK) {
        // Sub has been accepted
        return GG_ERR_OK;
    }
    if (gg_buffer_eq(GG_STR("BROKEN"), status)
        || gg_buffer_eq(GG_STR("FINISHED"), status)
        || gg_buffer_eq(GG_STR("RUNNING"), status)) {
        GG_LOGD("Sending early response.");
        ggl_sub_respond(
            handle,
            gg_obj_map(GG_MAP(
                gg_kv(GG_STR("component_name"), *component_name_obj),
                gg_kv(GG_STR("lifecycle_state"), gg_obj_buf(status))
            ))
        );
    }

    return GG_ERR_OK;
}

static GgError restart_component(void *ctx, GgMap params, uint32_t handle) {
    (void) ctx;
    GgObject *component_name_obj;
    GgError ret = gg_map_validate(
        params,
        GG_MAP_SCHEMA({ GG_STR("component_name"),
                        GG_REQUIRED,
                        GG_TYPE_BUF,
                        &component_name_obj })
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("restart_component received invalid arguments.");
        return GG_ERR_INVALID;
    }
    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GG_LOGE("`component_name` too long");
        return GG_ERR_RANGE;
    }

    GgError error = gghealthd_restart_component(component_name);
    if (error != GG_ERR_OK) {
        return error;
    }

    ggl_respond(handle, GG_OBJ_NULL);
    return GG_ERR_OK;
}

GgError run_gghealthd(void) {
    GgError error = gghealthd_init();
    if (error != GG_ERR_OK) {
        return error;
    }
    static GglRpcMethodDesc handlers[]
        = { { GG_STR("get_status"), false, get_status, NULL },
            { GG_STR("update_status"), false, update_status, NULL },
            { GG_STR("get_health"), false, get_health, NULL },
            { GG_STR("restart_component"), false, restart_component, NULL },
            { GG_STR("subscribe_to_deployment_updates"),
              true,
              subscribe_to_deployment_updates,
              NULL },
            { GG_STR("subscribe_to_lifecycle_completion"),
              true,
              subscribe_to_lifecycle_completion,
              NULL } };
    static const size_t HANDLERS_LEN = sizeof(handlers) / sizeof(handlers[0]);

    GgError ret = ggl_listen(GG_STR("gg_health"), handlers, HANDLERS_LEN);
    GG_LOGE("Exiting with error %u.", (unsigned) ret);

    return GG_ERR_FAILURE;
}
