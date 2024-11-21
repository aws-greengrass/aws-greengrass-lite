// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
#define GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H

#include <ggl/buffer.h>
#include <ggl/error.h>

#define MAX_THING_NAME_LEN 128

GglError publish_fleet_status_update(GglBuffer thing_name, GglBuffer trigger);

#endif // GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
