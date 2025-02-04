// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggipcd.h"
#include <argp.h>
#include <ggl/error.h>
#include <ggl/version.h>

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

static char doc[] = "ggipcd -- Greengrass IPC server for Nucleus Lite";

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

    GglError ret = run_ggipcd(&args);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
