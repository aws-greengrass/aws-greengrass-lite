/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGDEPLOYMENTD_LOCALDEPLOY_H
#define GGDEPLOYMENTD_LOCALDEPLOY_H

#include "args.h"
#include "ggl/error.h"
#include "deployment_model.h"

GglError ggdeploymentd_create_local_deployment(const GgdeploymentdLocalDeployment *local_deployment);

#endif
