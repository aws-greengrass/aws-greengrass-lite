// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
#define GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H

#include <ggl/error.h>
#include <ggl/object.h>

GglError publish_message(GglBuffer thing_name);

#endif // GG_FLEET_STATUSD_FLEET_STATUS_SERVICE_H
