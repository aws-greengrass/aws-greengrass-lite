// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONFIG_OPERATIONS_H
#define CONFIG_OPERATIONS_H

#include "fleet-provisioning.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdbool.h>

GglError ggl_update_iot_endpoints(FleetProvArgs *args);
GglError ggl_has_provisioning_config(GglArena alloc, bool *prov_enabled);
GglError ggl_is_already_provisioned(GglArena alloc, bool *provisioned);
GglError ggl_get_configuration(FleetProvArgs *args);
GglError ggl_update_system_cert_paths(
    GglBuffer output_dir_path, FleetProvArgs *args, GglBuffer thing_name
);

#endif
