// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CONFIG_OPERATIONS_H
#define CONFIG_OPERATIONS_H

#include <fleet-provisioning.h>
#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

GgError ggl_update_iot_endpoints(FleetProvArgs *args);
GgError ggl_has_provisioning_config(GgArena alloc, bool *prov_enabled);
GgError ggl_is_already_provisioned(GgArena alloc, bool *provisioned);
GgError ggl_get_configuration(FleetProvArgs *args);
GgError ggl_load_template_params(
    FleetProvArgs *args, GgArena *alloc, GgMap *template_params
);
GgError ggl_update_system_config(
    GgBuffer output_dir_path, FleetProvArgs *args, GgBuffer thing_name
);
GgError ggl_update_system_cert_path(
    GgBuffer output_dir_path, FleetProvArgs *args
);

#endif
