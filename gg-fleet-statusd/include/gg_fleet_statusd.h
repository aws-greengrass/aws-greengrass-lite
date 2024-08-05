// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GG_FLEET_STATUSD_H
#define GG_FLEET_STATUSD_H

#include <ggl/error.h>

typedef struct {
    char *thing_name;
} FssdArgs;

GglError run_gg_fleet_statusd(FssdArgs *args);

#endif
