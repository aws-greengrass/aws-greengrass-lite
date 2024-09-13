// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/recipe2unit.h"
#include "recipe2unit-test.h"
#include <ggl/error.h>

GglError run_recipe2unit_test(void) {
    Recipe2UnitArgs args = { 0 };
    char recipe_path[] = "./recipe2unit_python/recipe.yml";
    char root_dir[] = ".";

    args.gg_root_ca_path = "/home/test/rootCA.pem";
    args.recipe_path = recipe_path;
    args.root_dir = root_dir;
    args.user = "ubuntu";
    args.group = "ubuntu";
    args.recipe_runner_path = "/home/recpierunner";
    args.socket_path = "home/test/socket";
    args.aws_container_auth_token = "AAAAAAAA";
    args.ggc_version = "1.0.0";
    args.thing_name = "test";
    args.aws_container_cred_url = "12.43.1.1";
    args.aws_region = "us-west-2";

    convert_to_unit(&args);
    return GGL_ERR_OK;
}
