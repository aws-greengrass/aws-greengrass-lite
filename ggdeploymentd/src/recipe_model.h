/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGDEPLOYMENTD_RECIPEMODEL_H
#define GGDEPLOYMENTD_RECIPEMODEL_H

#include "ggl/object.h"

// TODO: this is random for now, figure out size needed for mem allocation when
// decoding json
#define GGL_RECIPE_CONTENT_MAX_SIZE 128

typedef struct {
    // TODO: check what this needs to be, this is probably wrong
    GglObject default_configuration;
} ComponentConfiguration;

typedef struct {
    GglBuffer os;
    GglBuffer architecture;
    GglBuffer nucleus_type;
} Platform;

typedef struct {
    GglBuffer name;
    Platform platform;
    GglList artifacts;
} PlatformManifest;

typedef struct {
    GglBuffer format_version;
    GglBuffer component_name;
    GglBuffer component_version;
    GglBuffer component_description;
    GglBuffer component_publisher;
    GglBuffer component_type;
    GglBuffer component_source;
    GglList manifests;
    GglMap component_dependencies;
    ComponentConfiguration configuration;
} Recipe;

#endif
