// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggdeploymentd.h"
#include <argp.h>
#include <ggl/error.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>

static char doc[] = "ggdeploymentd -- Greengrass Lite deployment daemon";

static struct argp_option opts[] = {
    { 0 },
};

// NOLINTNEXTLINE(readability-non-const-parameter)
static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    (void) arg;
    (void) state;
    switch (key) {
    case ARGP_KEY_END:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

int main(int argc, char **argv) {
    if (argc < 1) {
        return 1;
    }

    char bin_path[256] = { 0 };
    strncat(bin_path, argv[0], strlen(argv[0]));
    if (strlen(bin_path) < 1) {
        return 1;
    }
    bool slash_found = false;
    for (size_t i = strlen(bin_path); i > 0; i--) {
        if (bin_path[i - 1] == '/') {
            bin_path[i] = '\0';
            slash_found = true;
            break;
        }
    }
    if (!slash_found) {
        bin_path[0] = '\0';
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    GglError ret = run_ggdeploymentd(bin_path);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
