// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <ggipc/client.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

int main(void) {
    // Get the SocketPath from Environment Variable
    char *socket_path
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");
    if (socket_path == NULL) {
        GGL_LOGE("IPC socket path env var not set.");
        return 1;
    }

    // Connect to Greengrass IPC
    static uint8_t svcuid_mem[PATH_MAX];
    GglBuffer svcuid = GGL_BUF(svcuid_mem);
    svcuid.len = GGL_IPC_MAX_SVCUID_LEN;
    int conn = -1;
    GglError err = ggipc_connect_auth(
        ggl_buffer_from_null_term(socket_path), &svcuid, &conn
    );
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to connect to IPC server: %s", ggl_strerror(err));
        return 1;
    }
    GGL_LOGI("Connected to Greengrass IPC server.");

    GGL_LOGI("Hello World!");

    return 0;
}
