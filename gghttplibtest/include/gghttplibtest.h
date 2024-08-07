// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIBTEST_H
#define GGHTTPLIBTEST_H

#include <ggl/error.h>

typedef struct {
    char *thing_name;
    char *url;
    char *file_path;
    char *rootca;
    char *cert;
    char *key;
} GgHttpLibArgs;

GglError test_gghttplib(GgHttpLibArgs *args);

#endif
