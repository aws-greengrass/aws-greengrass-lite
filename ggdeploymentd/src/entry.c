// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_handler.h"
#include "ggdeploymentd.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <pthread.h>
#include <stdlib.h>

GglError run_ggdeploymentd(GgdeploymentdArgs *args) {
    (void) args;
    GGL_LOGI("ggdeploymentd", "Started ggdeploymentd process.");

    pthread_t ptid;
    pthread_create(&ptid, NULL, &ggl_deployment_handler_start, NULL);

    ggdeploymentd_start_server();

    pthread_join(ptid, NULL);
    return GGL_ERR_OK;
}
