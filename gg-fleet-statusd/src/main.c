/* gravel - Utilities for AWS IoT Core clients
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "args.h"
#include "fleet_status_service.h"
#include <argp.h>
// #include "ggl-lib/include/ggl/object.h"
// #include "ggl-lib/include/ggl/server.h"
#include "ggl/object.h"
#include "ggl/server.h"

static char doc[] = "gg-fleet-statusd -- Fleet Status Service for GG Lite";

static struct argp_option opts[]
    = { { "thingName", 't', "name", 0, "Thing Name", 0 }, { 0 } };

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    FssdArgs *args = state->input;
    switch (key) {
    case 't':
        args->thing_name = arg;
        break;
    case ARGP_KEY_END:
        if (args->thing_name == NULL) {
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
    FssdArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    // send_fleet_status_update_for_all_components(STARTUP);
    publish_message(args.thing_name);
}
