/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "args.h"
#include "deployment_queue.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include "ggl/log.h"
#include <ggl/core_bus/server.h>
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

static void create_local_deployment(void *ctx, GglMap params, GglResponseHandle handle);

static void create_local_deployment(void *ctx, GglMap params, GglResponseHandle handle) {
    (void) ctx;

    GGL_LOGD("ggdeploymentd", "Handling CreateLocalDeployment request.");
}

int main(int argc, char **argv) {
    GgdeploymentdArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    deployment_queue_init();

    GglRpcMethodDesc handlers[] = {
        { GGL_STR("publish"), false, create_local_deployment, NULL }
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret
        = ggl_listen(GGL_STR("/aws/ggl/ggdeploymentd"), handlers, handlers_len);

    GGL_LOGE("ggdeploymentd", "Exiting with error %u.", (unsigned) ret);
}
