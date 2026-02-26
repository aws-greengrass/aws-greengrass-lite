// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
#define GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H

#include <gg/error.h>
#include <gg/types.h>

#define MAX_THING_NAME_LEN 128

GgError publish_fleet_status_update(
    GgBuffer thing_name, GgBuffer trigger, GgMap deployment_info
);

#endif // GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
