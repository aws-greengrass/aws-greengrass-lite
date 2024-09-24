// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_handler.h"
#include "ggdeploymentd.h"
#include "iot_jobs_listener.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_PATH_LENGTH 256

static void *job_listener_thread(void *ctx) {
    (void) ctx;
    listen_for_jobs_deployments();
    return NULL;
}

GglError run_ggdeploymentd(const char *bin_path) {
    GGL_LOGI("ggdeploymentd", "Started ggdeploymentd process.");

    static uint8_t root_path_mem[MAX_PATH_LENGTH] = { 0 };
    GglBuffer root_path = GGL_BUF(root_path_mem);
    GglError ret = ggl_gg_config_read_str(
        (GglBuffer[2]) { GGL_STR("system"), GGL_STR("rootPath") }, 2, &root_path
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get root path from config.");
        return ret;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("ggdeploymentd", "Failed to open root_path.");
        return ret;
    }

    GglDeploymentHandlerThreadArgs args = { .root_path_fd = root_path_fd,
                                            .root_path = root_path,
                                            .bin_path = bin_path };

    pthread_t ptid_jobs;
    pthread_create(&ptid_jobs, NULL, &job_listener_thread, &args);
    pthread_detach(ptid_jobs);

    pthread_t ptid_handler;
    pthread_create(&ptid_handler, NULL, &ggl_deployment_handler_thread, &args);
    pthread_detach(ptid_handler);

    ggdeploymentd_start_server();

    return GGL_ERR_OK;
}
