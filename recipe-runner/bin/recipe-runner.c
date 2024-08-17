// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "recipe-runner.h"
#include <argp.h>
#include <ggl/error.h>
#include <stdlib.h>

static char doc[] = "recipe-runner -- Acts as a mediator that runs the ggl "
                    "deployment's Script section";

static struct argp_option opts[] = {
    { "filepath", 'p', "path", 0, "Provide path to a bash script file", 0 },
    { "component-name", 'n', "name", 0, "Name of the component being run", 0 },
    { 0 }
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    RecipeRunnerArgs *args = state->input;
    switch (key) {
    case 'p':
        args->file_path = arg;
        break;
    case 'n':
        args->component_name = arg;
        break;

    case ARGP_KEY_END:
        if (args->file_path == NULL || args->component_name == NULL) {
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

    GglError ret = run_recipe_runner(&args);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
    return GGL_ERR_OK;
}