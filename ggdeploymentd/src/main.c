/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "args.h"
#include "deployment_queue.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include <argp.h>
#include <stdlib.h>

static char doc[] = "ggdeploymentd -- Greengrass Lite Deployment Daemon";

static struct argp_option opts[]
        = { { "endpoint", 'e', "address", 0, "AWS IoT Core Dataplane endpoint", 0 },
            { 0 } };

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    GgdeploymentdArgs *args = state->input;
    switch (key) {
        case 'e':
            args->endpoint = arg;
            break;
        case ARGP_KEY_END:
            if (args->endpoint == NULL) {
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
    GgdeploymentdArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    deployment_queue_init();

    ggl_listen(GGL_STR("/aws/ggl/ggdeploymentd"), NULL);
}