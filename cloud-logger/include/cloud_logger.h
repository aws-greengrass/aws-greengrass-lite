// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CLOUD_LOGGER_H
#define CLOUD_LOGGER_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/vector.h>
#include <stdio.h>

#define MAX_LINE_LENGTH (2048)

GglError read_log(FILE *fp, GglObjVec *filling, GglAlloc *alloc);

#endif
