// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
#define GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H

#include <ggl/buffer.h>
#include <ggl/error.h>

#define MAX_THING_NAME_LEN 128

typedef struct {
    GglBuffer thing_name;
    GglBuffer trigger;
} GglFleetStatusServiceThreadArgs;

GglError publish_fleet_status_update(GglFleetStatusServiceThreadArgs *args);

void *ggl_fleet_status_service_thread(void *ctx);

#endif // GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
