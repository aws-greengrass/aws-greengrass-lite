// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_handler.h"
#include "ggdeploymentd.h"
#include "iot_jobs_listener.h"
#include "sys/stat.h"
#include "unistd.h"
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GglError run_ggdeploymentd(const char *bin_path) {
    GGL_LOGI("Started ggdeploymentd process.");

    umask(0002);

    static uint8_t root_path_mem[PATH_MAX] = { 0 };
    GglBuffer root_path = GGL_BUF(root_path_mem);
    root_path.len -= 1;
    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &root_path
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get root path from config.");
        return ret;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open rootPath.");
        return ret;
    }

    int sys_ret = fchdir(root_path_fd);
    if (sys_ret != 0) {
        GGL_LOGE("Failed to enter rootPath: %d.", errno);
        return GGL_ERR_FAILURE;
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
