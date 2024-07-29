// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_server.h"
#include <argp.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <stdlib.h>

typedef struct {
    char *socket_path;
} GglIpcArgs;

static char doc[] = "ggipcd -- Greengrass IPC server for Greengrass Lite";

static struct argp_option opts[] = {
    { "socket", 's', "path", 0, "GG IPC socket path", 0 },
    { 0 },
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    GglIpcArgs *args = state->input;
    switch (key) {
    case 's':
        args->socket_path = arg;
        break;
    case ARGP_KEY_END:
        if (args->socket_path == NULL) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            argp_usage(state);
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

int main(int argc, char **argv) {
    GglIpcArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    GglError err = ggl_ipc_listen(args.socket_path);

    GGL_LOGE("ipc-server", "Exiting due to error while listening (%u).", err);
    return 1;
}
