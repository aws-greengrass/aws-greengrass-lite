// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_MODEL_H
#define GGDEPLOYMENTD_DEPLOYMENT_MODEL_H

#include <gg/types.h>

#define MAX_COMP_NAME_BUF_SIZE 10000

typedef enum {
    GGL_DEPLOYMENT_QUEUED,
    GGL_DEPLOYMENT_IN_PROGRESS,
} GglDeploymentState;

typedef enum {
    INSTALL,
    RUN_STARTUP,
    BOOTSTRAP
} PhaseSelection;

typedef enum {
    LOCAL_DEPLOYMENT,
    THING_GROUP_DEPLOYMENT,
} GglDeploymentType;

typedef struct {
    GgBuffer deployment_id;
    GgBuffer recipe_directory_path;
    GgBuffer artifacts_directory_path;
    GgBuffer configuration_arn;
    GgBuffer thing_group;
    GglDeploymentState state;
    // Map of component names to map of component information, in cloud
    // deployment doc format
    GgMap components;
    GglDeploymentType type;
} GglDeployment;

#endif
