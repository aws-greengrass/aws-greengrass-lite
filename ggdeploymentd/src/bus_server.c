// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_model.h"
#include "ggl/log.h"
#include <ggl/core_bus/server.h>
#include <sys/time.h>

static void create_local_deployment(void *ctx, GglMap params, GglResponseHandle handle);

static void create_local_deployment(void *ctx, GglMap params, GglResponseHandle handle) {
    (void) ctx;

    GGL_LOGD("ggdeploymentd", "Handling CreateLocalDeployment request.");

    GgdeploymentdDeploymentDocument local_deployment_document = { 0 };

    GglObject *val;

    if (ggl_map_get(params, GGL_STR("recipe_directory_path"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.recipe_directory_path = val->buf;
    }

    if (ggl_map_get(params, GGL_STR("artifact_directory_path"), &val)) {
        // TODO: Validate format of artifact path string is:
        // /path/to/artifact/folder/component-name/component-version/artifacts
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.artifact_directory_path = val->buf;
    }

    if (ggl_map_get(params, GGL_STR("root_component_versions_to_add"), &val)) {
        if (val->type != GGL_TYPE_MAP) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.root_component_versions_to_add = val->map;
    }

    if (ggl_map_get(params, GGL_STR("root_components_to_remove"), &val)) {
        if (val->type != GGL_TYPE_LIST) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.root_components_to_remove = val->list;
    }

    if (ggl_map_get(params, GGL_STR("component_to_configuration"), &val)) {
        if (val->type != GGL_TYPE_MAP) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.component_to_configuration = val->map;
    }

    if (ggl_map_get(params, GGL_STR("component_to_run_with_info"), &val)) {
        if (val->type != GGL_TYPE_MAP) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.component_to_run_with_info = val->map;
    }

    if (ggl_map_get(params, GGL_STR("group_name"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            GGL_LOGE("ggdeploymentd", "CreateLocalDeployment received invalid arguments.");
            ggl_return_err(handle, GGL_ERR_INVALID);
            return;
        }
        local_deployment_document.group_name = val->buf;
    }

    // TODO: Revisit and see if local deployment should have a different value
    local_deployment_document.deployment_id = GGL_STR("LOCAL");

    struct timeval time;
    gettimeofday(&time, NULL);
    int64_t millis_from_seconds = (int64_t)(time.tv_sec) * 1000;
    int64_t millis_from_microseconds = (time.tv_usec / 1000);
    local_deployment_document.timestamp = millis_from_seconds + millis_from_microseconds;

    // TODO: Add remaining fields for cloud deployments

    GgdeploymentdDeployment new_deployment = { 0 };
    new_deployment.deployment_document = local_deployment_document;
    new_deployment.deployment_id = local_deployment_document.deployment_id;
    new_deployment.deployment_stage = DEFAULT;
    new_deployment.deployment_type = LOCAL;
    new_deployment.is_cancelled = false;

    deployment_queue_offer(new_deployment);

    ggl_respond(handle, GGL_OBJ_NULL());
}

void ggdeploymentd_start_server(void) {
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("create_local_deployment"), false, create_local_deployment, NULL }
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret
        = ggl_listen(GGL_STR("/aws/ggl/ggdeploymentd"), handlers, handlers_len);

    GGL_LOGE("ggdeploymentd", "Exiting with error %u.", (unsigned) ret);
}