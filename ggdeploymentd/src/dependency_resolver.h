// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPENDENCY_RESOLVER_H
#define GGDEPLOYMENTD_DEPENDENCY_RESOLVER_H

#include "deployment_model.h"
#include <ggl/error.h>
#include <ggl/object.h>

GglList resolve_dependencies(GglDeployment *deployment);

#endif
