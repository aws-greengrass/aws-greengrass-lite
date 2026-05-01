// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_MODEL_H
#define GGDEPLOYMENTD_DEPLOYMENT_MODEL_H

#include <gg/types.h>
#include <gg/vector.h>
#include <stdbool.h>

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
    GgBuffer iot_job_id;
    GgBuffer recipe_directory_path;
    GgBuffer artifacts_directory_path;
    GgBuffer configuration_arn;
    GgBuffer thing_group;
    GglDeploymentState state;
    // Map of component names to map of component information, in cloud
    // deployment doc format
    GgMap components;
    GglDeploymentType type;
    // Map of component names to configuration merge payloads, from
    // CreateLocalDeployment IPC `componentToConfiguration` field.
    GgMap component_to_configuration;
} GglDeployment;

/// Local runtime state for a deployment, separate from the cloud deployment
/// document. Populated by the deployment handler and read by the caller.
typedef struct {
    bool is_bootstrap;
    GgBuffer source_iot_data_endpoint;
    /// Optional output: components that cleanup_stale_versions fully removed
    /// during this deployment. The caller owns the backing buffers; the
    /// handler appends names so the post-deployment fleet status update can
    /// report them as UNINSTALLED. Both pointers must be NULL or both must
    /// be non-NULL.
    GgByteVec *removed_names_storage;
    GgBufVec *removed_components;
} DeploymentContext;

#endif
