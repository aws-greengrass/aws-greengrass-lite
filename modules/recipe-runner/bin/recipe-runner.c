// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "recipe-runner.h"
#include <argp.h>
#include <ggl/error.h>
#include <ggl/nucleus/init.h>
#include <stdlib.h>

static char doc[] = "recipe-runner -- Launch a Greengrass recipe file";

static struct argp_option opts[] = {
    { "phase", 'p', "name", 0, "Provide phase you want to execute", 0 },
    { "component-name", 'n', "name", 0, "Name of the component being run", 0 },
    { "component-version", 'v', "version", 0, "Version of the component", 0 },
    { 0 }
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    RecipeRunnerArgs *args = state->input;
    switch (key) {
    case 'p':
        args->phase = arg;
        break;
    case 'n':
        args->component_name = arg;
        break;
    case 'v':
        args->component_version = arg;
        break;

    case ARGP_KEY_END:
        if (args->phase == NULL || args->component_name == NULL
            || args->component_version == NULL) {
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
    static RecipeRunnerArgs args = { 0 };

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    ggl_nucleus_init();

    GglError ret = run_recipe_runner(&args);
    return ret != GGL_ERR_OK;
}
