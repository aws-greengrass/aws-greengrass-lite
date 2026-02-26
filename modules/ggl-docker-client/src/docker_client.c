/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gg/arena.h>
#include <gg/base64.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/io.h>
#include <gg/json_decode.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/api_ecr.h>
#include <ggl/docker_client.h>
#include <ggl/exec.h>
#include <ggl/http.h>
#include <ggl/uri.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GgError head_buf_write(void *context, GgBuffer buf) {
    GgByteVec *output = (GgByteVec *) context;
    GgBuffer remaining = gg_byte_vec_remaining_capacity(*output);
    buf = gg_buffer_substr(buf, 0, remaining.len);
    (void) gg_byte_vec_append(output, buf);
    return GG_ERR_OK;
}

// Captures the first N bytes of a payload. The rest are silently discarded.
static GgWriter head_buf_writer(GgByteVec *vec) {
    return (GgWriter) { .ctx = vec, .write = head_buf_write };
}

/// The max length of a docker image name including its repository and digest
#define DOCKER_MAX_IMAGE_LEN (4096U)

GgError ggl_docker_check_server(void) {
    const char *args[] = { "docker", "-v", NULL };
    uint8_t output_bytes[512U] = { 0 };
    GgByteVec output = GG_BYTE_VEC(output_bytes);
    GgError err = ggl_exec_command_with_output(args, head_buf_writer(&output));
    if (err != GG_ERR_OK) {
        if (output.buf.len == 0) {
            GG_LOGE("Docker does not appear to be installed.");
        } else {
            GG_LOGE(
                "docker -v failed with '%.*s'",
                (int) output.buf.len,
                output.buf.data
            );
        }
    }

    return err;
}

GgError ggl_docker_pull(GgBuffer image_name) {
    char image_null_term[DOCKER_MAX_IMAGE_LEN + 1U] = { 0 };
    if (image_name.len > DOCKER_MAX_IMAGE_LEN) {
        GG_LOGE("Docker image name too long.");
        return GG_ERR_INVALID;
    }
    memcpy(image_null_term, image_name.data, image_name.len);

    GG_LOGD("Pulling %.*s", (int) image_name.len, image_name.data);
    const char *args[] = { "docker", "pull", "-q", image_null_term, NULL };
    GgError err = ggl_exec_command(args);
    if (err != GG_ERR_OK) {
        GG_LOGE("docker image pull failed.");
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError ggl_docker_remove(GgBuffer image_name) {
    char image_null_term[DOCKER_MAX_IMAGE_LEN + 1U] = { 0 };
    if (image_name.len > DOCKER_MAX_IMAGE_LEN) {
        GG_LOGE("Docker image name too long.");
        return GG_ERR_INVALID;
    }
    GG_LOGD("Removing docker image '%s'", image_null_term);

    memcpy(image_null_term, image_name.data, image_name.len);
    const char *args[] = { "docker", "rmi", image_null_term, NULL };

    uint8_t output_bytes[512U] = { 0 };
    GgByteVec output = GG_BYTE_VEC(output_bytes);
    GgError err = ggl_exec_command_with_output(args, head_buf_writer(&output));
    if (err != GG_ERR_OK) {
        size_t start = 0;
        if (gg_buffer_contains(output.buf, GG_STR("No such image"), &start)) {
            GG_LOGD("Image was not found to delete.");
            return GG_ERR_OK;
        }
        GG_LOGE(
            "docker rmi failed: '%.*s'", (int) output.buf.len, output.buf.data
        );
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError ggl_docker_check_image(GgBuffer image_name) {
    char image_null_term[DOCKER_MAX_IMAGE_LEN + 1U] = { 0 };
    if (image_name.len > DOCKER_MAX_IMAGE_LEN) {
        GG_LOGE("Docker image name too long.");
        return GG_ERR_INVALID;
    }
    memcpy(image_null_term, image_name.data, image_name.len);

    GG_LOGD("Finding docker image '%s'", image_null_term);

    const char *args[]
        = { "docker", "image", "ls", "-q", image_null_term, NULL };

    uint8_t output_bytes[256] = { 0 };
    GgByteVec output = GG_BYTE_VEC(output_bytes);
    GgError err = ggl_exec_command_with_output(args, head_buf_writer(&output));
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "docker image ls -q failed: '%.*s'",
            (int) output.buf.len,
            output.buf.data
        );
        return GG_ERR_FAILURE;
    }
    if (output.buf.len == 0) {
        return GG_ERR_NOENTRY;
    }
    return GG_ERR_OK;
}

GgError ggl_docker_credentials_store(
    GgBuffer registry, GgBuffer username, GgBuffer secret
) {
    char registry_buf[4096 + 1] = { 0 };
    if (registry.len >= sizeof(registry_buf)) {
        GG_LOGE("Registry name too long.");
        return GG_ERR_INVALID;
    }
    char username_buf[4096 + 1] = { 0 };
    if (username.len >= sizeof(username_buf)) {
        GG_LOGE("Docker username too long");
        return GG_ERR_INVALID;
    }
    memcpy(registry_buf, registry.data, registry.len);
    memcpy(username_buf, username.data, username.len);

    const char *const ARGS[] = { "docker",     "login",      registry_buf,
                                 "--username", username_buf, "--password-stdin",
                                 NULL };
    return ggl_exec_command_with_input(ARGS, gg_obj_buf(secret));
}

GgError ggl_docker_credentials_ecr_retrieve(
    GglDockerUriInfo ecr_registry, SigV4Details sigv4_details
) {
    GG_LOGI("Requesting ECR credentials");
    sigv4_details.aws_service = GG_STR("ecr");
    // https://github.com/aws/containers-roadmap/issues/1589
    // Not sure how to size this buffer as the size of a token appears to be
    // unbounded.
    static uint8_t response_buf[8000];
    GgBuffer response = GG_BUF(response_buf);

    uint16_t http_response = 400;
    GgError err = ggl_http_ecr_get_authorization_token(
        sigv4_details, &http_response, &response
    );

    if ((err != GG_ERR_OK) || (http_response != 200U)) {
        GG_LOGE(
            "GetAuthorizationToken failed (HTTP %" PRIu16 "): %.*s",
            http_response,
            (int) response.len,
            response.data
        );
        return GG_ERR_FAILURE;
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
    GgArena arena = gg_arena_init(GG_BUF(secret_arena));
    GgObject response_obj = GG_OBJ_NULL;
    err = gg_json_decode_destructive(response, &arena, &response_obj);
    if ((err != GG_ERR_OK) || (gg_obj_type(response_obj)) != GG_TYPE_MAP) {
        return GG_ERR_INVALID;
    }
    GgObject *token_list_obj = NULL;
    if (!gg_map_get(
            gg_obj_into_map(response_obj),
            GG_STR("authorizationData"),
            &token_list_obj
        )) {
        GG_LOGE("Response parse failure.");
        return GG_ERR_INVALID;
    }
    if (gg_obj_type(*token_list_obj) != GG_TYPE_LIST) {
        GG_LOGE("Response i not a list of maps.");

        return GG_ERR_INVALID;
    }
    GgList token_list = gg_obj_into_list(*token_list_obj);
    if (token_list.len == 0) {
        GG_LOGE("Response is empty.");

        return GG_ERR_FAILURE;
    }

    err = gg_list_type_check(token_list, GG_TYPE_MAP);
    if (err != GG_ERR_OK) {
        GG_LOGE("Response not a list of maps.");
        return GG_ERR_INVALID;
    }

    GG_LIST_FOREACH (token_map, token_list) {
        GgObject *token_obj = NULL;
        GgObject *registry_obj = NULL;
        err = gg_map_validate(
            gg_obj_into_map(*token_map),
            GG_MAP_SCHEMA(
                {
                    GG_STR("authorizationToken"),
                    GG_REQUIRED,
                    GG_TYPE_BUF,
                    &token_obj,
                },
                { GG_STR("proxyEndpoint"),
                  GG_OPTIONAL,
                  GG_TYPE_BUF,
                  &registry_obj }
            )
        );
        if (err != GG_ERR_OK) {
            GG_LOGE("Token not found in response");

            return GG_ERR_FAILURE;
        }
        GgBuffer token = gg_obj_into_buf(*token_obj);
        bool decoded = gg_base64_decode_in_place(&token);
        if (decoded != true) {
            GG_LOGE("Token was not base64");

            return GG_ERR_PARSE;
        }
        size_t split;
        if (!gg_buffer_contains(token, GG_STR(":"), &split)) {
            GG_LOGE("Token was not user:pass");

            return GG_ERR_PARSE;
        }

        GgBuffer registry = (registry_obj != NULL)
            ? gg_obj_into_buf(*registry_obj)
            : ecr_registry.repository;
        GgBuffer username = gg_buffer_substr(token, 0, split);
        GgBuffer secret = gg_buffer_substr(token, split + 1U, SIZE_MAX);
        err = ggl_docker_credentials_store(registry, username, secret);
        if (err != GG_ERR_OK) {
            GG_LOGE("Failed to store docker credentials.");
            return GG_ERR_FAILURE;
        }
    }
    return GG_ERR_OK;
}

bool ggl_docker_is_uri_private_ecr(GglDockerUriInfo docker_uri) {
    // The URL for the default private registry is
    // <aws_account_id>.dkr.ecr.<region>.amazonaws.com
    return gg_buffer_has_prefix(
        gg_buffer_substr(
            docker_uri.registry, GG_STR("012345678901").len, SIZE_MAX
        ),
        GG_STR(".dkr.ecr.")
    );
}
