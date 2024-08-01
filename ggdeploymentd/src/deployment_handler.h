// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_HANDLER_H
#define GGDEPLOYMENTD_DEPLOYMENT_HANDLER_H

#include "deployment_model.h"
#include "recipe_model.h"
#include <ggl/error.h>
#include <ggl/object.h>

void *ggl_deployment_handler_start(void *ctx);
void ggl_deployment_handler_stop(void);
void listen(void);
void handle_deployment(GgdeploymentdDeployment);
GglError load_recipe(GglBuffer recipe_dir);
GglError load_artifact(GglBuffer artifact_dir);
GglError read_recipe(char *recipe_path, Recipe *recipe);
GglError parse_recipe(GglMap recipe_map, Recipe *recipe);

#endif
