// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGIPCD_H
#define GGIPCD_H

#include <gg/error.h>

typedef struct {
    char *socket_path;
} GglIpcArgs;

GgError run_ggipcd(GglIpcArgs *args);

#endif
