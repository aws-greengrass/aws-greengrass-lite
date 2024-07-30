// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include <ggl/core_bus/server.h>

static void create_local_deployment(void *ctx, GglMap params, GglResponseHandle handle);

static void create_local_deployment(void *ctx, GglMap params, GglResponseHandle handle) {
    (void) ctx;

    GGL_LOGD("ggdeploymentd", "Handling CreateLocalDeployment request.");
}

void ggdeploymentd_start_server(void) {
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("publish"), false, create_local_deployment, NULL }
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret
        = ggl_listen(GGL_STR("/aws/ggl/ggdeploymentd"), handlers, handlers_len);

    GGL_LOGE("ggdeploymentd", "Exiting with error %u.", (unsigned) ret);
}