// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_handler.h"
#include "iot_jobs_listener.h"
#include <errno.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggdeploymentd.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/proxy/environment.h>
#include <limits.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

GgError run_ggdeploymentd(const char *bin_path) {
    GG_LOGI("Started ggdeploymentd process.");

    GgError ret = ggl_proxy_set_environment();
    if (ret != GG_ERR_OK) {
        return ret;
    }

    umask(0002);

    static uint8_t root_path_mem[PATH_MAX] = { 0 };
    GgArena alloc = gg_arena_init(
        gg_buffer_substr(GG_BUF(root_path_mem), 0, sizeof(root_path_mem) - 1)
    );
    GgBuffer root_path;
    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootPath")), &alloc, &root_path
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get root path from config.");
        return ret;
    }

    int root_path_fd;
    ret = gg_dir_open(root_path, O_PATH, false, &root_path_fd);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open rootPath.");
        return ret;
    }

    int sys_ret = fchdir(root_path_fd);
    if (sys_ret != 0) {
        GG_LOGE("Failed to enter rootPath: %d.", errno);
        (void) gg_close(root_path_fd);
        return GG_ERR_FAILURE;
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

    return GG_ERR_OK;
}
