// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttplibtest.h"
#include <argp.h>
#include <ggl/error.h>
#include <stdlib.h>

static char doc[] = "gghttplib -- Greengrass Lite httplib for curl";

static struct argp_option opts[]
    = { { "thing_name", 't', "name", 0, "Aws Iot thing name", 0 },
        { "url", 'u', "address", 0, "AWS IoT Core endpoint", 0 },
        { "file_path", 'f', "path", 0, "local file path", 0 },
        { "rootca", 'r', "path", 0, "Path to AWS IoT Core CA PEM", 0 },
        { "cert", 'c', "path", 0, "Path to client certificate", 0 },
        { "key", 'k', "path", 0, "Path to key for client certificate", 0 },
        { 0 } };

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    GgHttpLibArgs *args = state->input;
    switch (key) {
    case 't':
        args->thing_name = arg;
        break;
    case 'u':
        args->url = arg;
        break;
    case 'f':
        args->file_path = arg;
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
        if (args->url == NULL) {
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
    static GgHttpLibArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    GglError ret = test_gghttplib(&args);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
