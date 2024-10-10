// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "s3-get-test.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/http.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

GglError run_s3_test(char *region, char *bucket, char *key, char *file_path) {
    static uint8_t big_buffer_for_bump[4096];
    GglError request_ret = GGL_ERR_OK;
    {
        static GglBuffer tesd = GGL_STR("/aws/ggl/tesd");
        GglObject result;
        GglMap params = { 0 };
        GglBumpAlloc the_allocator
            = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

        static char url_buffer[256];
        GglByteVec url_vec = GGL_BYTE_VEC(url_buffer);
        GglError error = GGL_ERR_OK;
        ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("https://"));
        ggl_byte_vec_chain_append(
            &error, &url_vec, ggl_buffer_from_null_term(bucket)
        );
        ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".s3."));
        ggl_byte_vec_chain_append(
            &error, &url_vec, ggl_buffer_from_null_term(region)
        );
        ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR(".amazonaws.com/"));
        ggl_byte_vec_chain_append(
            &error, &url_vec, ggl_buffer_from_null_term(key)
        );
        ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("\0"));

        if (error != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        error = ggl_call(
            tesd,
            GGL_STR("request_credentials"),
            params,
            NULL,
            &the_allocator.alloc,
            &result
        );
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        GglObject *aws_access_key_id = NULL;
        GglObject *aws_secret_access_key = NULL;
        GglObject *aws_session_token = NULL;

        if (result.type != GGL_TYPE_MAP) {
            GGL_LOGE("Result not a map");
            return GGL_ERR_FAILURE;
        }

        GglError ret = ggl_map_validate(
            result.map,
            GGL_MAP_SCHEMA(
                { GGL_STR("accessKeyId"),
                  true,
                  GGL_TYPE_BUF,
                  &aws_access_key_id },
                { GGL_STR("secretAccessKey"),
                  true,
                  GGL_TYPE_BUF,
                  &aws_secret_access_key },
                { GGL_STR("sessionToken"),
                  true,
                  GGL_TYPE_BUF,
                  &aws_session_token },
            )
        );
        if (ret != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        int fd = -1;
        request_ret = ggl_file_open(
            ggl_buffer_from_null_term(file_path),
            O_CREAT | O_WRONLY | O_TRUNC,
            0644,
            &fd
        );
        if (request_ret != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        request_ret = sigv4_download(
            url_buffer,
            fd,
            (SigV4Details) {
                .aws_region = ggl_buffer_from_null_term(region),
                .aws_service = GGL_STR("s3"),
                .access_key_id = aws_access_key_id->buf,
                .secret_access_key = aws_secret_access_key->buf,
                .session_token = aws_session_token->buf,
            }
        );
    }

    int fd = 0;
    GglError file_ret
        = ggl_file_open(ggl_buffer_from_null_term(file_path), 0, O_RDONLY, &fd);
    if ((file_ret == GGL_ERR_OK) && (fd > 0)) {
        if (GGL_LOG_LEVEL >= GGL_LOG_DEBUG) {
            for (;;) {
                ssize_t bytes_read = read(
                    fd, big_buffer_for_bump, sizeof(big_buffer_for_bump)
                );
                if (bytes_read <= 0) {
                    close(fd);
                    break;
                }
                GGL_LOGD("%.*s", (int) bytes_read, big_buffer_for_bump);
            }
        }

        (void) ggl_close(fd);
    }

    if ((request_ret != GGL_ERR_OK) || (file_ret != GGL_ERR_OK)) {
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}
