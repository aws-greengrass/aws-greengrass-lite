/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/docker_client.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/log.h>
#include <string.h>
#include <stddef.h>

/// The max length of a docker image name including its repository and digest
#define DOCKER_MAX_IMAGE_LEN (4096U)

GglError ggl_docker_check_server(void) {
    const char *args[] = { "docker", "-v", NULL };
    uint8_t output_bytes[512U] = { 0 };
    GglBuffer output = GGL_BUF(output_bytes);
    GglError err = ggl_exec_command_with_output(args, &output);
    if (err != GGL_ERR_OK) {
        if (output.len == 0) {
            GGL_LOGE("Docker does not appear to be installed.");
        } else {
            GGL_LOGE(
                "docker -v failed with '%.*s'", (int) output.len, output.data
            );
        }
    }

    return err;
}

GglError ggl_docker_pull(GglBuffer image_name) {
    char image_null_term[DOCKER_MAX_IMAGE_LEN + 1U] = { 0 };
    if (image_name.len > DOCKER_MAX_IMAGE_LEN) {
        GGL_LOGE("Docker image name too long.");
        return GGL_ERR_INVALID;
    }
    memcpy(image_null_term, image_name.data, image_name.len);
    const char *args[] = { "docker", "pull", "-q", image_null_term, NULL };
    uint8_t output_bytes[DOCKER_MAX_IMAGE_LEN + 256U] = { 0 };
    GglBuffer output = GGL_BUF(output_bytes);
    GglError err = ggl_exec_command_with_output(args, &output);
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "docker image ls -q failed: '%.*s'", (int) output.len, output.data
        );
        return err;
    }
    return GGL_ERR_OK;
}

GglError ggl_docker_remove(GglBuffer image_name) {
    char image_null_term[DOCKER_MAX_IMAGE_LEN + 1U] = { 0 };
    if (image_name.len > DOCKER_MAX_IMAGE_LEN) {
        GGL_LOGE("Docker image name too long.");
        return GGL_ERR_INVALID;
    }
    GGL_LOGD("Removing docker image '%s'", image_null_term);

    memcpy(image_null_term, image_name.data, image_name.len);
    const char *args[] = { "docker", "rmi", image_null_term, NULL };

    uint8_t output_bytes[512U] = { 0 };
    GglBuffer output = GGL_BUF(output_bytes);
    GglError err = ggl_exec_command_with_output(args, &output);
    if (err != GGL_ERR_OK) {
        size_t start = 0;
        if (ggl_buffer_contains(output, GGL_STR("No such image"), &start)) {
            GGL_LOGD("Image was not found to delete.");
            return GGL_ERR_OK;
        }
        GGL_LOGE("docker rmi failed: '%.*s'", (int) output.len, output.data);
        return err;
    }
    return GGL_ERR_OK;
}

GglError ggl_docker_check_image(GglBuffer image_name) {
    char image_null_term[DOCKER_MAX_IMAGE_LEN + 1U] = { 0 };
    if (image_name.len > DOCKER_MAX_IMAGE_LEN) {
        GGL_LOGE("Docker image name too long.");
        return GGL_ERR_INVALID;
    }
    memcpy(image_null_term, image_name.data, image_name.len);

    GGL_LOGD("Finding docker image '%s'", image_null_term);

    const char *args[]
        = { "docker", "image", "ls", "-q", image_null_term, NULL };

    uint8_t output_bytes[512] = { 0 };
    GglBuffer output = GGL_BUF(output_bytes);
    GglError err = ggl_exec_command_with_output(args, &output);
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "docker image ls -q failed: '%.*s'", (int) output.len, output.data
        );
        return err;
    }
    if (output.len == 0) {
        return GGL_ERR_NOENTRY;
    }
    return GGL_ERR_OK;
}
