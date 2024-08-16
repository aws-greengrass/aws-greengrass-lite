// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "iotcored.h"
#include <argp.h>
#include <ggl/error.h>

static char doc[] = "iotcored -- MQTT spooler for AWS IoT Core";

static struct argp_option opts[] = {
    { "interface_name", 'n', "name", 0, "Override core bus interface name", 0 },
    { "endpoint", 'e', "address", 0, "AWS IoT Core endpoint", 0 },
    { "id", 'i', "name", 0, "MQTT client identifier", 0 },
    { "rootca", 'r', "path", 0, "Path to AWS IoT Core CA PEM", 0 },
    { "cert", 'c', "path", 0, "Path to client certificate", 0 },
    { "key", 'k', "path", 0, "Path to key for client certificate", 0 },
    { 0 }
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    IotcoredArgs *args = state->input;
    switch (key) {
    case 'n':
        args->interface_name = arg;
        break;
    case 'e':
        args->endpoint = arg;
        break;
    case 'i':
        args->id = arg;
        break;
    case 'r':
        args->rootca = arg;
        break;
    case 'c':
        args->cert = arg;
        break;
    case 'k':
        args->key = arg;
        break;
    case ARGP_KEY_END:
        // ALL keys have defaults further in.
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

int main(int argc, char **argv) {
    static IotcoredArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    GglError ret = run_iotcored(&args);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
