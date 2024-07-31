// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENTMODEL_H
#define GGDEPLOYMENTD_DEPLOYMENTMODEL_H

#include "args.h"
#include "ggl/error.h"
#include "ggl/object.h"

enum DeploymentStage {
    DEFAULT = 0,
    BOOTSTRAP = 1,
    KERNEL_ACTIVATION = 2,
    KERNEL_ROLLBACK = 3,
    ROLLBACK_BOOTSTRAP = 4
};

enum DeploymentType {
    LOCAL = 0,
    SHADOW = 1,
    IOT_JOBS = 2
};

enum DeploymentStatus {
    SUCCESSFUL = 0,
    FAILED_NO_STATE_CHANGE = 1,
    FAILED_ROLLBACK_NOT_REQUESTED = 2,
    FAILED_ROLLBACK_COMPLETE = 3,
    FAILED_UNABLE_TO_ROLLBACK = 4,
    REJECTED = 5
};

typedef struct {
    uint64_t timeout;
    GglBuffer action;
} GgdeploymentdComponentUpdatePolicy;

typedef struct {
    uint64_t timeout_in_seconds;
    uint64_t serial_version_uid;
} GgdeploymentdDeploymentConfigValidationPolicy;

typedef struct {
    GglBuffer recipe_directory_path;
    GglBuffer artifact_directory_path;
    GglMap root_component_versions_to_add;
    GglList root_components_to_remove;
    GglMap component_to_configuration;
    GglMap component_to_run_with_info;
    GglBuffer group_name;
    GglBuffer deployment_id;
    int64_t timestamp;
    GglBuffer configuration_arn;
    GglList required_capabilities;
    GglBuffer on_behalf_of;
    GglBuffer parent_group_name;
    GglBuffer failure_handling_policy;
    GgdeploymentdComponentUpdatePolicy component_update_policy;
    GgdeploymentdDeploymentConfigValidationPolicy
        deployment_config_validation_policy;
} GgdeploymentdDeploymentDocument;

typedef struct {
    GgdeploymentdDeploymentDocument deployment_document;
    GglBuffer deployment_id;
    enum DeploymentStage deployment_stage;
    enum DeploymentType deployment_type;
    bool is_cancelled;
    GglList error_stack;
    GglList error_types;
} GgdeploymentdDeployment;

typedef struct {
    enum DeploymentStatus deployment_status;
} GgdeploymentDeploymentResult;

#endif
