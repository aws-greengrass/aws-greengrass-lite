// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "args.h"
#include "bus_server.h"
#include "deployment_handler.h"
#include "deployment_queue.h"
#include <argp.h>
#include <ggl/log.h>
#include <pthread.h>
#include <stdlib.h>

static char doc[] = "ggdeploymentd -- Greengrass Lite Deployment Daemon";

static struct argp_option opts[]
    = { { "endpoint", 'e', "address", 0, "AWS IoT Core endpoint", 0 }, { 0 } };

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
    GGL_LOGI("ggdeploymentd", "Started ggdeploymentd process.");
    GgdeploymentdArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    pthread_t ptid;
    pthread_create(&ptid, NULL, &ggl_deployment_handler_start, NULL);

    ggdeploymentd_start_server();

    // TODO: Maybe instead of waiting for the other thread, just consider
    // ggdeploymentd errored if the main thread reaches this point.
    pthread_join(ptid, NULL);
}
