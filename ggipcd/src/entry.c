// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipcd.h"
#include "ipc_server.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <stdlib.h>

GglError run_ggipcd(GglIpcArgs *args) {
    const char *socket_path
        = (args->socket_path != NULL) ? args->socket_path : "./gg-ipc.socket";
    GglError err = ggl_ipc_listen(socket_path);

    GGL_LOGE("ipc-server", "Exiting due to error while listening (%u).", err);
    return err;
}
