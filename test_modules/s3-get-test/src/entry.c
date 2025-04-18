// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "s3-get-test.h"
#include <sys/types.h>
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
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
    static uint8_t alloc_mem[4096];
    GglError request_ret = GGL_ERR_OK;
    {
        static GglBuffer tesd = GGL_STR("aws_iot_tes");
        GglObject result;
        GglMap params = { 0 };
        GglArena alloc = ggl_arena_init(GGL_BUF(alloc_mem));

        static char host[256];
        GglByteVec host_vec = GGL_BYTE_VEC(host);
        GglError error = GGL_ERR_OK;
        ggl_byte_vec_chain_append(
            &error, &host_vec, ggl_buffer_from_null_term(bucket)
        );
        ggl_byte_vec_chain_append(&error, &host_vec, GGL_STR(".s3."));
        ggl_byte_vec_chain_append(
            &error, &host_vec, ggl_buffer_from_null_term(region)
        );
        ggl_byte_vec_chain_append(&error, &host_vec, GGL_STR(".amazonaws.com"));

        static char url_buffer[256];
        GglByteVec url_vec = GGL_BYTE_VEC(url_buffer);
        ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("https://"));
        ggl_byte_vec_chain_append(
            &error,
            &url_vec,
            (GglBuffer) { .data = host_vec.buf.data, .len = host_vec.buf.len }
        );
        ggl_byte_vec_chain_push(&error, &url_vec, '/');
        ggl_byte_vec_chain_append(
            &error, &url_vec, ggl_buffer_from_null_term(key)
        );
        ggl_byte_vec_chain_append(&error, &url_vec, GGL_STR("\0"));

        if (error != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        error = ggl_call(
            tesd, GGL_STR("request_credentials"), params, NULL, &alloc, &result
        );
        if (error != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        GglObject *aws_access_key_id_obj = NULL;
        GglObject *aws_secret_access_key_obj = NULL;
        GglObject *aws_session_token_obj = NULL;

        if (ggl_obj_type(result) != GGL_TYPE_MAP) {
            GGL_LOGE("Result not a map");
            return GGL_ERR_FAILURE;
        }

        GglMap result_map = ggl_obj_into_map(result);

        GglError ret = ggl_map_validate(
            result_map,
            GGL_MAP_SCHEMA(
                { GGL_STR("accessKeyId"),
                  true,
                  GGL_TYPE_BUF,
                  &aws_access_key_id_obj },
                { GGL_STR("secretAccessKey"),
                  true,
                  GGL_TYPE_BUF,
                  &aws_secret_access_key_obj },
                { GGL_STR("sessionToken"),
                  true,
                  GGL_TYPE_BUF,
                  &aws_session_token_obj },
            )
        );
        if (ret != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }

        GglBuffer aws_access_key_id = ggl_obj_into_buf(*aws_access_key_id_obj);
        GglBuffer aws_secret_access_key
            = ggl_obj_into_buf(*aws_secret_access_key_obj);
        GglBuffer aws_session_token = ggl_obj_into_buf(*aws_session_token_obj);

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
        uint16_t http_response_code;

        request_ret = sigv4_download(
            url_buffer,
            (GglBuffer) { .data = host_vec.buf.data, .len = host_vec.buf.len },
            ggl_buffer_from_null_term(key),
            fd,
            (SigV4Details) { .aws_region = ggl_buffer_from_null_term(region),
                             .aws_service = GGL_STR("s3"),
                             .access_key_id = aws_access_key_id,
                             .secret_access_key = aws_secret_access_key,
                             .session_token = aws_session_token },
            &http_response_code
        );
    }

    int fd = 0;
    GglError file_ret
        = ggl_file_open(ggl_buffer_from_null_term(file_path), 0, O_RDONLY, &fd);
    if ((file_ret == GGL_ERR_OK) && (fd > 0)) {
        if (GGL_LOG_LEVEL >= GGL_LOG_DEBUG) {
            while (true) {
                ssize_t bytes_read = read(fd, alloc_mem, sizeof(alloc_mem));
                if (bytes_read <= 0) {
                    close(fd);
                    break;
                }
                GGL_LOGD("%.*s", (int) bytes_read, alloc_mem);
            }
        }

        (void) ggl_close(fd);
    }

    if ((request_ret != GGL_ERR_OK) || (file_ret != GGL_ERR_OK)) {
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}
