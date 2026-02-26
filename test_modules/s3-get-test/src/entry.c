// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/core_bus/client.h>
#include <ggl/http.h>
#include <s3-get-test.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

GgError run_s3_test(char *region, char *bucket, char *key, char *file_path) {
    static uint8_t alloc_mem[4096];
    GgError request_ret = GG_ERR_OK;
    {
        static GgBuffer tesd = GG_STR("aws_iot_tes");
        GgObject result;
        GgMap params = { 0 };
        GgArena alloc = gg_arena_init(GG_BUF(alloc_mem));

        static char host[256];
        GgByteVec host_vec = GG_BYTE_VEC(host);
        GgError error = GG_ERR_OK;
        gg_byte_vec_chain_append(
            &error, &host_vec, gg_buffer_from_null_term(bucket)
        );
        gg_byte_vec_chain_append(&error, &host_vec, GG_STR(".s3."));
        gg_byte_vec_chain_append(
            &error, &host_vec, gg_buffer_from_null_term(region)
        );
        gg_byte_vec_chain_append(&error, &host_vec, GG_STR(".amazonaws.com"));

        static char url_buffer[256];
        GgByteVec url_vec = GG_BYTE_VEC(url_buffer);
        gg_byte_vec_chain_append(&error, &url_vec, GG_STR("https://"));
        gg_byte_vec_chain_append(
            &error,
            &url_vec,
            (GgBuffer) { .data = host_vec.buf.data, .len = host_vec.buf.len }
        );
        gg_byte_vec_chain_push(&error, &url_vec, '/');
        gg_byte_vec_chain_append(
            &error, &url_vec, gg_buffer_from_null_term(key)
        );
        gg_byte_vec_chain_append(&error, &url_vec, GG_STR("\0"));

        if (error != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }

        error = ggl_call(
            tesd, GG_STR("request_credentials"), params, NULL, &alloc, &result
        );
        if (error != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }

        GgObject *aws_access_key_id_obj = NULL;
        GgObject *aws_secret_access_key_obj = NULL;
        GgObject *aws_session_token_obj = NULL;

        if (gg_obj_type(result) != GG_TYPE_MAP) {
            GG_LOGE("Result not a map");
            return GG_ERR_FAILURE;
        }

        GgMap result_map = gg_obj_into_map(result);

        GgError ret = gg_map_validate(
            result_map,
            GG_MAP_SCHEMA(
                { GG_STR("accessKeyId"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &aws_access_key_id_obj },
                { GG_STR("secretAccessKey"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &aws_secret_access_key_obj },
                { GG_STR("sessionToken"),
                  GG_REQUIRED,
                  GG_TYPE_BUF,
                  &aws_session_token_obj },
            )
        );
        if (ret != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }

        GgBuffer aws_access_key_id = gg_obj_into_buf(*aws_access_key_id_obj);
        GgBuffer aws_secret_access_key
            = gg_obj_into_buf(*aws_secret_access_key_obj);
        GgBuffer aws_session_token = gg_obj_into_buf(*aws_session_token_obj);

        int fd = -1;
        request_ret = gg_file_open(
            gg_buffer_from_null_term(file_path),
            O_CREAT | O_WRONLY | O_TRUNC,
            0644,
            &fd
        );
        if (request_ret != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }
        uint16_t http_response_code;

        request_ret = sigv4_download(
            url_buffer,
            (GgBuffer) { .data = host_vec.buf.data, .len = host_vec.buf.len },
            gg_buffer_from_null_term(key),
            fd,
            (SigV4Details) { .aws_region = gg_buffer_from_null_term(region),
                             .aws_service = GG_STR("s3"),
                             .access_key_id = aws_access_key_id,
                             .secret_access_key = aws_secret_access_key,
                             .session_token = aws_session_token },
            &http_response_code
        );
    }

    int fd = 0;
    GgError file_ret
        = gg_file_open(gg_buffer_from_null_term(file_path), 0, O_RDONLY, &fd);
    if ((file_ret == GG_ERR_OK) && (fd > 0)) {
        if (GG_LOG_LEVEL >= GG_LOG_DEBUG) {
            while (true) {
                ssize_t bytes_read = read(fd, alloc_mem, sizeof(alloc_mem));
                if (bytes_read <= 0) {
                    close(fd);
                    break;
                }
                GG_LOGD("%.*s", (int) bytes_read, alloc_mem);
            }
        }

        (void) gg_close(fd);
    }

    if ((request_ret != GG_ERR_OK) || (file_ret != GG_ERR_OK)) {
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}
