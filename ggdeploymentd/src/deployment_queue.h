/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "localdeploy.h"
#include "deployment_model.h"
#include <stdint.h>

#ifndef GGDEPLOYMENTD_QUEUE_H
#define GGDEPLOYMENTD_QUEUE_H

void deployment_queue_init(void);
bool deployment_queue_offer(GgdeploymentdDeployment deployment);
GgdeploymentdDeployment deployment_queue_poll(void);
uint8_t deployment_queue_size(void);
#endif