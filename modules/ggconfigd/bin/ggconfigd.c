// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggconfigd.h"
#include <argp.h>
#include <ggl/buffer.h>
#include <ggl/nucleus/init.h>
#include <stdlib.h>

static char doc[] = "ggconfigd -- Greengrass Nucleus Lite configuration daemon";

static struct argp_option opts[] = {
    { "config-file", 'c', "path", 0, "Configuration file to use", 0 },
    { "config-dir", 'C', "path", 0, "Directory to look for config files", 0 },
    { 0 }
};

static GglBuffer config_path = GGL_STR("/etc/greengrass/config.yaml");
static GglBuffer config_dir = GGL_STR("/etc/greengrass/config.d");

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    (void) arg;
    (void) state;
    switch (key) {
    case 'c':
        config_path = ggl_buffer_from_null_term(arg);
        break;
    case 'C':
        config_dir = ggl_buffer_from_null_term(arg);
        break;
    case ARGP_KEY_END:
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

static void exit_cleanup(void) {
    (void) ggconfig_close();
}

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    ggl_nucleus_init();

    atexit(exit_cleanup);

    (void) ggconfig_open();

    // TODO: clean up error handling for these, and don't log missing files as
    // errors
    (void) ggconfig_load_file(config_path);
    (void) ggconfig_load_dir(config_dir);

    ggconfigd_start_server();

    return 1;
}
