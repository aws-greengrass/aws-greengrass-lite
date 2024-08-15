// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "health.h"
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LIFECYCLE_STATE_MAX_LEN (sizeof("INSTALLED") - 1U)

static void get_status(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GglObject *component_name = NULL;
    bool found
        = ggl_map_get(params, GGL_STR("component_name"), &component_name);
    if (!found || component_name->type != GGL_TYPE_BUF) {
        GGL_LOGE(
            "rpc-handler", "Missing required GGL_TYPE_BUF `component_name`"
        );
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }

    GglBuffer status;
    GglError error = gghealthd_get_status(component_name->buf, &status);
    if (error == GGL_ERR_OK) {
        GGL_LOGD(
            "gghealthd",
            "%.*s is %.*s",
            (int) component_name->buf.len,
            component_name->buf.data,
            (int) status.len,
            status.data
        );
        ggl_respond(
            handle,
            GGL_OBJ_MAP(
                { GGL_STR("component_name"), GGL_OBJ(component_name->buf) },
                { GGL_STR("lifecycle_state"), GGL_OBJ(status) },
            )
        );
    } else {
        ggl_return_err(handle, error);
    }
}

static void update_status(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    GglObject *component_name = NULL;
    bool found
        = ggl_map_get(params, GGL_STR("component_name"), &component_name);
    if (!found || (component_name->type != GGL_TYPE_BUF)) {
        GGL_LOGE(
            "rpc-handler", "Missing required GGL_TYPE_BUF `component_name`"
        );
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }
    if (component_name->buf.len > COMPONENT_NAME_MAX_LEN) {
        GGL_LOGE("rpc-handler", "`component_name` too long");
        ggl_return_err(handle, GGL_ERR_RANGE);
        return;
    }

    GglObject *lifecycle_state = NULL;
    found = ggl_map_get(params, GGL_STR("lifecycle_state"), &lifecycle_state);
    if (!found || (component_name->type != GGL_TYPE_BUF)) {
        GGL_LOGE(
            "rpc-handler", "Missing required GGL_TYPE_BUF `lifecycle_state`"
        );
        ggl_return_err(handle, GGL_ERR_INVALID);
        return;
    }
    if (lifecycle_state->buf.len > LIFECYCLE_STATE_MAX_LEN) {
        GGL_LOGE("rpc-handler", "`lifecycle_state` too long");
        ggl_return_err(handle, GGL_ERR_RANGE);
    }

    GglError error
        = gghealthd_update_status(component_name->buf, lifecycle_state->buf);
    if (error == GGL_ERR_OK) {
        ggl_respond(handle, GGL_OBJ_NULL());
    } else {
        ggl_return_err(handle, error);
    }
}

static void get_health(void *ctx, GglMap params, uint32_t handle) {
    (void) params;
    (void) ctx;
    GglBuffer status = { 0 };
    GglError error = gghealthd_get_health(&status);

    if (error == GGL_ERR_OK) {
        ggl_respond(handle, GGL_OBJ(status));
    } else {
        ggl_return_err(handle, error);
    }
}

static void subscribe_to_deployment_updates(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;
    (void) params;
    (void) handle;
}

GglError run_gghealthd(void) {
    GglError error = gghealthd_init();
    if (error != GGL_ERR_OK) {
        return error;
    }
    static GglRpcMethodDesc handlers[] = {
        { GGL_STR("get_status"), false, get_status, NULL },
        { GGL_STR("update_status"), false, update_status, NULL },
        { GGL_STR("get_health"), false, get_health, NULL },
        { GGL_STR("subscribe_to_deployment_updates"),
          true,
          subscribe_to_deployment_updates,
          NULL },
    };
    static const size_t HANDLERS_LEN = sizeof(handlers) / sizeof(handlers[0]);

    ggl_listen(GGL_STR("/aws/ggl/gghealthd"), handlers, HANDLERS_LEN);

    return GGL_ERR_OK;
}
