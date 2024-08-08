// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENTMODEL_H
#define GGDEPLOYMENTD_DEPLOYMENTMODEL_H

#include "ggdeploymentd.h"
#include "ggl/error.h"
#include "ggl/object.h"

enum DeploymentStage {
    GGDEPLOYMENT_DEFAULT = 0,
    GGDEPLOYMENT_BOOTSTRAP = 1,
    GGDEPLOYMENT_KERNEL_ACTIVATION = 2,
    GGDEPLOYMENT_KERNEL_ROLLBACK = 3,
    GGDEPLOYMENT_ROLLBACK_BOOTSTRAP = 4
};

enum DeploymentType {
    GGDEPLOYMENT_LOCAL = 0,
    GGDEPLOYMENT_SHADOW = 1,
    GGDEPLOYMENT_IOT_JOBS = 2
};

enum DeploymentStatus {
    GGDEPLOYMENT_SUCCESSFUL = 0,
    GGDEPLOYMENT_FAILED_NO_STATE_CHANGE = 1,
    GGDEPLOYMENT_FAILED_ROLLBACK_NOT_REQUESTED = 2,
    GGDEPLOYMENT_FAILED_ROLLBACK_COMPLETE = 3,
    GGDEPLOYMENT_FAILED_UNABLE_TO_ROLLBACK = 4,
    GGDEPLOYMENT_REJECTED = 5
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
} GgdeploymentdDeploymentResult;

#endif
