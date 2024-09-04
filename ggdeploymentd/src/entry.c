// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_handler.h"
#include "ggdeploymentd.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_PATH_LENGTH 128

static GglBuffer root_path = GGL_STR("/var/lib/aws-greengrass-v2");

static GglError update_root_path(void) {
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ_LIST(GGL_OBJ_STR("system"), GGL_OBJ_STR("rootPath")) }
    );

    static uint8_t resp_mem[MAX_PATH_LENGTH] = { 0 };
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(resp_mem));

    GglObject resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        NULL,
        &balloc.alloc,
        &resp
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("ggdeploymentd", "Failed to get root path from config.");
        if ((ret == GGL_ERR_NOMEM) || (ret == GGL_ERR_FATAL)) {
            return ret;
        }
        return GGL_ERR_OK;
    }
    if (resp.type != GGL_TYPE_BUF) {
        GGL_LOGE("ggdeploymentd", "Configuration root path is not a string.");
        return GGL_ERR_INVALID;
    }

    root_path = resp.buf;
    return GGL_ERR_OK;
}

GglError run_ggdeploymentd(const char *bin_path) {
    GGL_LOGI("ggdeploymentd", "Started ggdeploymentd process.");

    GglError ret = update_root_path();
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int root_path_fd;
    ret = ggl_dir_open(root_path, O_PATH, &root_path_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("ggdeploymentd", "Failed to open root_path.");
        return ret;
    }

    GglDeploymentHandlerThreadArgs args = { .root_path_fd = root_path_fd,
                                            .root_path = root_path,
                                            .bin_path = bin_path };

    pthread_t ptid;
    pthread_create(&ptid, NULL, &ggl_deployment_handler_thread, &args);
    pthread_detach(ptid);

    ggdeploymentd_start_server();

    return GGL_ERR_OK;
}
