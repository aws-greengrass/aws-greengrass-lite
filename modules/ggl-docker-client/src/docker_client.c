/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/docker_client.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdint.h>

static GglError head_buf_write(void *context, GglBuffer buf) {
    GglByteVec *output = (GglByteVec *) context;
    GglBuffer remaining = ggl_byte_vec_remaining_capacity(*output);
    buf = ggl_buffer_substr(buf, 0, remaining.len);
    (void) ggl_byte_vec_append(output, buf);
    return GGL_ERR_OK;
}

// Captures the first N bytes of a payload. The rest are silently discarded.
static GglWriter head_buf_writer(GglByteVec *vec) {
    return (GglWriter) { .ctx = vec, .write = head_buf_write };
}

/// The max length of a docker image name including its repository and digest
#define DOCKER_MAX_IMAGE_LEN (4096U)

GglError ggl_docker_check_server(void) {
    const char *args[] = { "docker", "-v", NULL };
    uint8_t output_bytes[512U] = { 0 };
    GglByteVec output = GGL_BYTE_VEC(output_bytes);
    GglError err = ggl_exec_command_with_output(args, head_buf_writer(&output));
    if (err != GGL_ERR_OK) {
        if (output.buf.len == 0) {
            GGL_LOGE("Docker does not appear to be installed.");
        } else {
            GGL_LOGE(
                "docker -v failed with '%.*s'",
                (int) output.buf.len,
                output.buf.data
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
    GglError err = ggl_exec_command(args);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("docker image ls -q failed.");
        return GGL_ERR_FAILURE;
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
    GglByteVec output = GGL_BYTE_VEC(output_bytes);
    GglError err = ggl_exec_command_with_output(args, head_buf_writer(&output));
    if (err != GGL_ERR_OK) {
        size_t start = 0;
        if (ggl_buffer_contains(output.buf, GGL_STR("No such image"), &start)) {
            GGL_LOGD("Image was not found to delete.");
            return GGL_ERR_OK;
        }
        GGL_LOGE(
            "docker rmi failed: '%.*s'", (int) output.buf.len, output.buf.data
        );
        return GGL_ERR_FAILURE;
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

    uint8_t output_bytes[256] = { 0 };
    GglByteVec output = GGL_BYTE_VEC(output_bytes);
    GglError err = ggl_exec_command_with_output(args, head_buf_writer(&output));
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "docker image ls -q failed: '%.*s'",
            (int) output.buf.len,
            output.buf.data
        );
        return GGL_ERR_FAILURE;
    }
    if (output.buf.len == 0) {
        return GGL_ERR_NOENTRY;
    }
    return GGL_ERR_OK;
}
