/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/docker_client.h"
#include <ggl/api_ecr.h>
#include <ggl/arena.h>
#include <ggl/base64.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/flags.h>
#include <ggl/http.h>
#include <ggl/io.h>
#include <ggl/json_decode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/uri.h>
#include <ggl/vector.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
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

    GGL_LOGD("Pulling %.*s", (int) image_name.len, image_name.data);
    const char *args[] = { "docker", "pull", "-q", image_null_term, NULL };
    GglError err = ggl_exec_command(args);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("docker image pull failed.");
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

GglError ggl_docker_credentials_store(
    GglBuffer registry, GglBuffer username, GglBuffer secret
) {
    char registry_buf[4096 + 1] = { 0 };
    if (registry.len >= sizeof(registry_buf)) {
        GGL_LOGE("Registry name too long.");
        return GGL_ERR_INVALID;
    }
    char username_buf[4096 + 1] = { 0 };
    if (username.len >= sizeof(username_buf)) {
        GGL_LOGE("Docker username too long");
        return GGL_ERR_INVALID;
    }
    memcpy(registry_buf, registry.data, registry.len);
    memcpy(username_buf, username.data, username.len);

    const char *const ARGS[] = { "docker",     "login",      registry_buf,
                                 "--username", username_buf, "--password-stdin",
                                 NULL };
    return ggl_exec_command_with_input(ARGS, ggl_obj_buf(secret));
}

GglError ggl_docker_credentials_ecr_retrieve(
    GglDockerUriInfo ecr_registry, SigV4Details sigv4_details
) {
    GGL_LOGI("Requesting ECR credentials");
    sigv4_details.aws_service = GGL_STR("ecr");
    // https://github.com/aws/containers-roadmap/issues/1589
    // Not sure how to size this buffer as the size of a token appears to be
    // unbounded.
    static uint8_t response_buf[8000];
    GglBuffer response = GGL_BUF(response_buf);

    uint16_t http_response = 400;
    GglError err = ggl_http_ecr_get_authorization_token(
        sigv4_details, &http_response, &response
    );

    if ((err != GGL_ERR_OK) || (http_response != 200U)) {
        GGL_LOGE(
            "GetAuthorizationToken failed (HTTP %" PRIu16 "): %.*s",
            http_response,
            (int) response.len,
            response.data
        );
        return GGL_ERR_FAILURE;
    }

    /*
        Response Syntax:
        {
            "authorizationData": [
                {
                    "authorizationToken": "string",
                    "expiresAt": number,
                    "proxyEndpoint": "string"
                }
            ]
        }
    */
    uint8_t secret_arena[512];
    GglArena arena = ggl_arena_init(GGL_BUF(secret_arena));
    GglObject response_obj = GGL_OBJ_NULL;
    err = ggl_json_decode_destructive(response, &arena, &response_obj);
    if ((err != GGL_ERR_OK) || (ggl_obj_type(response_obj)) != GGL_TYPE_MAP) {
        return GGL_ERR_INVALID;
    }
    GglObject *token_list_obj = NULL;
    if (!ggl_map_get(
            ggl_obj_into_map(response_obj),
            GGL_STR("authorizationData"),
            &token_list_obj
        )) {
        GGL_LOGE("Response parse failure.");
        return GGL_ERR_INVALID;
    }
    if (ggl_obj_type(*token_list_obj) != GGL_TYPE_LIST) {
        GGL_LOGE("Response i not a list of maps.");

        return GGL_ERR_INVALID;
    }
    GglList token_list = ggl_obj_into_list(*token_list_obj);
    if (token_list.len == 0) {
        GGL_LOGE("Response is empty.");

        return GGL_ERR_FAILURE;
    }

    err = ggl_list_type_check(token_list, GGL_TYPE_MAP);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Response not a list of maps.");
        return GGL_ERR_INVALID;
    }

    GGL_LIST_FOREACH (token_map, token_list) {
        GglObject *token_obj = NULL;
        GglObject *registry_obj = NULL;
        err = ggl_map_validate(
            ggl_obj_into_map(*token_map),
            GGL_MAP_SCHEMA(
                {
                    GGL_STR("authorizationToken"),
                    GGL_REQUIRED,
                    GGL_TYPE_BUF,
                    &token_obj,
                },
                { GGL_STR("proxyEndpoint"),
                  GGL_OPTIONAL,
                  GGL_TYPE_BUF,
                  &registry_obj }
            )
        );
        if (err != GGL_ERR_OK) {
            GGL_LOGE("Token not found in response");

            return GGL_ERR_FAILURE;
        }
        GglBuffer token = ggl_obj_into_buf(*token_obj);
        bool decoded = ggl_base64_decode_in_place(&token);
        if (decoded != true) {
            GGL_LOGE("Token was not base64");

            return GGL_ERR_PARSE;
        }
        size_t split;
        if (!ggl_buffer_contains(token, GGL_STR(":"), &split)) {
            GGL_LOGE("Token was not user:pass");

            return GGL_ERR_PARSE;
        }

        GglBuffer registry = (registry_obj != NULL)
            ? ggl_obj_into_buf(*registry_obj)
            : ecr_registry.repository;
        GglBuffer username = ggl_buffer_substr(token, 0, split);
        GglBuffer secret = ggl_buffer_substr(token, split + 1U, SIZE_MAX);
        err = ggl_docker_credentials_store(registry, username, secret);
        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed to store docker credentials.");
            return GGL_ERR_FAILURE;
        }
    }
    return GGL_ERR_OK;
}

bool ggl_docker_is_uri_private_ecr(GglDockerUriInfo docker_uri) {
    // The URL for the default private registry is
    // <aws_account_id>.dkr.ecr.<region>.amazonaws.com
    return ggl_buffer_has_prefix(
        ggl_buffer_substr(
            docker_uri.registry, GGL_STR("012345678901").len, SIZE_MAX
        ),
        GGL_STR(".dkr.ecr.")
    );
}
