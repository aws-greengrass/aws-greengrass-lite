// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipcd.h"
#include "ipc_server.h"
#include <ggl/error.h>
#include <ggl/log.h>

GglError run_ggipcd(GglIpcArgs *args) {
    GglError err = ggl_ipc_listen(args->socket_path);

    GGL_LOGE("ipc-server", "Exiting due to error while listening (%u).", err);
    return err;
}
