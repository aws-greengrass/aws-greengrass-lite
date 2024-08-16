// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DEPLOYMENT_HANDLER_H
#define GGDEPLOYMENTD_DEPLOYMENT_HANDLER_H

#include <ggl/error.h>
#include <ggl/object.h>

void *ggl_deployment_handler_thread(void *ctx);
void ggl_deployment_handler_stop(void);

#endif
