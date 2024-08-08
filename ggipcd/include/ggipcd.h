// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGIPCD_H
#define GGIPCD_H

#include <ggl/error.h>

typedef struct {
    char *socket_path;
} GglIpcArgs;

GglError run_ggipcd(GglIpcArgs *args);

#endif
