// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_DATAPLANE_MANAGER_H
#define GGDEPLOYMENTD_DATAPLANE_MANAGER_H

#include <ggl/buffer.h>
#include <ggl/error.h>

GglError make_dataplane_call(
    GglBuffer uri_path, char *body, GglBuffer *response
);
GglError resolve_component_with_cloud(
    GglBuffer component_name,
    GglBuffer version_requirements,
    GglBuffer *response
);
GglError get_device_thing_groups(GglBuffer *response);

#endif
