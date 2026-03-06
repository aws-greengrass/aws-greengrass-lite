// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// tesd -- Token Exchange Service for AWS credential desperse management

#include <argp.h>
#include <gg/error.h>
#include <ggl/nucleus/init.h>
#include <tesd.h>

static char doc[] = "tesd -- Token Exchange Service daemon";

static struct argp_option opts[] = {
    { "interface_name", 'n', "name", 0, "Override core bus interface name", 0 },
    { "endpoint", 'e', "address", 0, "IoT credential endpoint", 0 },
    { "role_alias", 'a', "alias", 0, "IoT role alias", 0 },
    { 0 }
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    TesdArgs *args = state->input;
    switch (key) {
    case 'n':
        args->interface_name = arg;
        break;
    case 'e':
        args->cred_endpoint = arg;
        break;
    case 'a':
        args->role_alias = arg;
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
    static TesdArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    ggl_nucleus_init();

    GgError ret = run_tesd(&args);
    if (ret != GG_ERR_OK) {
        return 1;
    }
}
