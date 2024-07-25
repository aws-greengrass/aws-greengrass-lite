/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggconfig.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include <argp.h>
#include <stdlib.h>

static char doc[] = "ggconfigd -- configuration management";
static struct argp_option opts[] = { { 0 } };

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    return ARGP_ERR_UNKNOWN;
}
static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, 0);

    //     ggconfig_open();

    ggl_listen(GGL_STR("/aws/ggl/ggconfigd"), NULL);

    /*
     ggconfig_close();
    */
}
