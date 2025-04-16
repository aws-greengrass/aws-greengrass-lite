// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <ggipc/client.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <inttypes.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>

#define INT64_DECIMAL_DIGITS_MAX (sizeof("-9223372036854775808") - 1)

static GglBuffer component_name = GGL_STR("ggipc.client.test");

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;
    // Get the SocketPath from Environment Variable
    char *socket_path
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");

    if (socket_path == NULL) {
        GGL_LOGE("IPC socket path env var not set.");
        return GGL_ERR_FAILURE;
    }

    int conn = -1;
    GglError ret = ggipc_connect_by_name(
        ggl_buffer_from_null_term(socket_path), component_name, NULL, &conn
    );
    if (ret != GGL_ERR_OK) {
        return 1;
    }

    struct timespec t = { 0 };
    int err = clock_gettime(CLOCK_REALTIME, &t);
    if (err == -1) {
        GGL_LOGE("clock_gettime() on CLOCK_REALTIME failed with %d", errno);
    }

    GGL_LOGT(
        "Putting timestamp ( %" PRIi64 ") into config.", (int64_t) t.tv_sec
    );
    ret = ggipc_update_config(
        conn,
        GGL_BUF_LIST(GGL_STR("timestamp")),
        &t,
        ggl_obj_i64((int64_t) t.tv_sec)
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write timestamp.");
        return 1;
    }

    GGL_LOGT("Reading timestamp out of config.");
    GglObject timestamp_obj = ggl_obj_i64(-1);

    uint8_t ipc_bytes[128 + (((INT64_DECIMAL_DIGITS_MAX + 2) / 3) * 4)] = { 0 };

    GglBuffer ipc_buf = GGL_BUF(ipc_bytes);
    {
        GglBumpAlloc balloc = ggl_bump_alloc_init(ipc_buf);
        ret = ggipc_get_config_obj(
            conn,
            GGL_BUF_LIST(GGL_STR("timestamp")),
            NULL,
            &balloc.alloc,
            &timestamp_obj
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to read timestamp.");
            return 1;
        }

        if ((ggl_obj_type(timestamp_obj) != GGL_TYPE_I64)
            || (ggl_obj_into_i64(timestamp_obj) != (int64_t) t.tv_sec)) {
            GGL_LOGE("Mismatched timestamp.");
            return 1;
        }
    }

    {
        GGL_LOGT("Publishing timestamp as object.");
        ret = ggipc_publish_to_topic_obj(
            conn, GGL_STR("test_topic"), ggl_obj_i64((int64_t) t.tv_sec)
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to publish object.");
            return 1;
        }
    }

    {
        GGL_LOGT("Publishing timestamp as buffer.");

        GglBuffer timestamp_buf
            = GGL_BUF((uint8_t[INT64_DECIMAL_DIGITS_MAX]) { 0 });

        int written = snprintf(
            (char *) timestamp_buf.data,
            timestamp_buf.len,
            "%" PRIi64,
            (int64_t) t.tv_sec
        );
        if (written <= 0) {
            return 1;
        }

        timestamp_buf.len = (size_t) written;

        GglBumpAlloc balloc = ggl_bump_alloc_init(ipc_buf);
        ret = ggipc_publish_to_topic_binary(
            conn, GGL_STR("test_topic2"), timestamp_buf, &balloc.alloc
        );

        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to publish buffer.");
            return 1;
        }
    }

    if (ret == GGL_ERR_OK) {
        GGL_LOGI("Test succeeded");
    }
    return ret != GGL_ERR_OK;
}
