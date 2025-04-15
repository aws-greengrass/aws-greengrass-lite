// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_H
#define IOTCORED_H

#include <ggl/error.h>

typedef struct {
    char *interface_name;
    char *endpoint;
    char *id;
    char *rootca;
    char *cert;
    char *key;
    char *no_proxy;
    char *proxy_uri;
} IotcoredArgs;

GglError run_iotcored(IotcoredArgs *args);

#endif
