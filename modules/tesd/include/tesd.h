// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef TESD_H
#define TESD_H

#include <gg/error.h>

typedef struct {
    char *interface_name;
    char *cred_endpoint;
    char *role_alias;
} TesdArgs;

GgError run_tesd(TesdArgs *args);

#endif
