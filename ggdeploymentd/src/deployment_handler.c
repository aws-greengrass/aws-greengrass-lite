// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include "deployment_handler.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <fcntl.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static GglError merge_dir_to(
    GglBuffer source, int root_path_fd, GglBuffer subdir
) {
    int source_fd;
    GglError ret = ggl_dir_open(source, O_PATH, &source_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, source_fd);

    int dest_fd;
    ret = ggl_dir_openat(root_path_fd, subdir, O_PATH, &dest_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_DEFER(close, dest_fd);

    return ggl_copy_dir(source_fd, dest_fd);
}

static void handle_deployment(
    GglDeployment *deployment, GglDeploymentHandlerThreadArgs *args
) {
    int root_path_fd = args->root_path_fd;
    if (deployment->recipe_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->recipe_directory_path,
            root_path_fd,
            GGL_STR("/packages/recipes")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to copy recipes.");
            return;
        }
    }

    if (deployment->artifact_directory_path.len != 0) {
        GglError ret = merge_dir_to(
            deployment->artifact_directory_path,
            root_path_fd,
            GGL_STR("/packages/artifacts")
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ggdeploymentd", "Failed to copy artifacts.");
            return;
        }
    }

    // TODO: Handle component changes
}

static GglError ggl_deployment_listen(GglDeploymentHandlerThreadArgs *args) {
    while (true) {
        GglDeployment *deployment;
        // Since this is blocking, error is fatal
        GglError ret = ggl_deployment_dequeue(&deployment);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        GGL_LOGI("deployment-handler", "Processing incoming deployment.");

        handle_deployment(deployment, args);

        GGL_LOGD("deployment-handler", "Completed deployment.");

        ggl_deployment_release(deployment);
    }
}

void *ggl_deployment_handler_thread(void *ctx) {
    GGL_LOGD("deployment-handler", "Starting deployment processing thread.");

    (void) ggl_deployment_listen(ctx);

    GGL_LOGE("deployment-handler", "Deployment thread exiting due to failure.");

    // This is safe as long as only this thread will ever call exit.

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    exit(1);

    return NULL;
}
