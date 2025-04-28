// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "health.h"
#include "subscriptions.h"
#include <ggl/buffer.h>
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/nucleus/constants.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIFECYCLE_STATE_MAX_LEN (sizeof("INSTALLED") - 1U)

static GglError get_status(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GglObject *component_name_obj;
    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA({ GGL_STR("component_name"),
                         GGL_REQUIRED,
                         GGL_TYPE_BUF,
                         &component_name_obj })
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("get_status received invalid arguments.");
        return GGL_ERR_INVALID;
    }
    GglBuffer component_name = ggl_obj_into_buf(*component_name_obj);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GGL_LOGE("`component_name` too long");
        return GGL_ERR_RANGE;
    }

    GglBuffer status;
    GglError error = gghealthd_get_status(component_name, &status);
    if (error != GGL_ERR_OK) {
        return error;
    }

    GGL_LOGD(
        "%.*s is %.*s",
        (int) component_name.len,
        component_name.data,
        (int) status.len,
        status.data
    );
    ggl_respond(
        handle,
        ggl_obj_map(GGL_MAP(
            { GGL_STR("component_name"), ggl_obj_buf(component_name) },
            { GGL_STR("lifecycle_state"), ggl_obj_buf(status) },
        ))
    );
    return GGL_ERR_OK;
}

static GglError update_status(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GglObject *component_name_obj;
    GglObject *state_obj;
    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA(
            { GGL_STR("component_name"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &component_name_obj },
            { GGL_STR("lifecycle_state"),
              GGL_REQUIRED,
              GGL_TYPE_BUF,
              &state_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("update_status received invalid arguments.");
        return GGL_ERR_INVALID;
    }
    GglBuffer component_name = ggl_obj_into_buf(*component_name_obj);
    GglBuffer state = ggl_obj_into_buf(*state_obj);

    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GGL_LOGE("`component_name` too long");
        return GGL_ERR_RANGE;
    }
    if (state.len > LIFECYCLE_STATE_MAX_LEN) {
        GGL_LOGE("`lifecycle_state` too long");
        return GGL_ERR_RANGE;
    }

    GglError error = gghealthd_update_status(component_name, state);
    if (error != GGL_ERR_OK) {
        return error;
    }

    ggl_respond(handle, GGL_OBJ_NULL);
    return GGL_ERR_OK;
}

static GglError get_health(void *ctx, GglMap params, uint32_t handle) {
    (void) params;
    (void) ctx;
    GglBuffer status = { 0 };
    GglError error = gghealthd_get_health(&status);

    if (error != GGL_ERR_OK) {
        return error;
    }

    ggl_respond(handle, ggl_obj_buf(status));
    return GGL_ERR_OK;
}

// TODO: implement or remove this
static GglError subscribe_to_deployment_updates(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;
    (void) params;
    (void) handle;
    return GGL_ERR_UNSUPPORTED;
}

static GglError subscribe_to_lifecycle_completion(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;
    GglObject *component_name_obj = NULL;
    GglError ret = ggl_map_validate(
        params,
        GGL_MAP_SCHEMA({ GGL_STR("component_name"),
                         GGL_REQUIRED,
                         GGL_TYPE_BUF,
                         &component_name_obj })
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("subscribe_to_lifecycle_completion received invalid arguments."
        );
        return GGL_ERR_INVALID;
    }
    GglBuffer component_name = ggl_obj_into_buf(*component_name_obj);
    if (component_name.len > GGL_COMPONENT_NAME_MAX_LEN) {
        GGL_LOGE("`component_name` too long");
        return GGL_ERR_RANGE;
    }

    ret = gghealthd_register_lifecycle_subscription(component_name, handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer status;
    GglError error = gghealthd_get_status(component_name, &status);
    if (error != GGL_ERR_OK) {
        // Sub has been accepted
        return GGL_ERR_OK;
    }
    if (ggl_buffer_eq(GGL_STR("BROKEN"), status)
        || ggl_buffer_eq(GGL_STR("FINISHED"), status)
        || ggl_buffer_eq(GGL_STR("RUNNING"), status)) {
        GGL_LOGD("Sending early response.");
        ggl_sub_respond(
            handle,
            ggl_obj_map(GGL_MAP(
                { GGL_STR("component_name"), *component_name_obj },
                { GGL_STR("lifecycle_state"), ggl_obj_buf(status) }
            ))
        );
    }

    return GGL_ERR_OK;
}

GglError run_gghealthd(void) {
    GglError error = gghealthd_init();
    if (error != GGL_ERR_OK) {
        return error;
    }
    static GglRpcMethodDesc handlers[]
        = { { GGL_STR("get_status"), false, get_status, NULL },
            { GGL_STR("update_status"), false, update_status, NULL },
            { GGL_STR("get_health"), false, get_health, NULL },
            { GGL_STR("subscribe_to_deployment_updates"),
              true,
              subscribe_to_deployment_updates,
              NULL },
            { GGL_STR("subscribe_to_lifecycle_completion"),
              true,
              subscribe_to_lifecycle_completion,
              NULL } };
    static const size_t HANDLERS_LEN = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret = ggl_listen(GGL_STR("gg_health"), handlers, HANDLERS_LEN);
    GGL_LOGE("Exiting with error %u.", (unsigned) ret);

    return GGL_ERR_FAILURE;
}
