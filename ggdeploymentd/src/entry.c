// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_queue.h"
#include "ggdeploymentd.h"
#include <ggl/error.h>
#include <ggl/log.h>

GglError run_ggdeploymentd(GgdeploymentdArgs *args) {
    (void) args;
    GGL_LOGI("ggdeploymentd", "Started ggdeploymentd process.");
    ggl_deployment_queue_init();
    ggdeploymentd_start_server();
    return GGL_ERR_OK;
}
