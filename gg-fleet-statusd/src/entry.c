// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet_status_service.h"
#include "gg_fleet_statusd.h"
#include <ggl/error.h>

GglError run_gg_fleet_statusd(FssdArgs *args) {
    // send_fleet_status_update_for_all_components(STARTUP);
    publish_message(args->thing_name);
    return GGL_ERR_FAILURE;
}
