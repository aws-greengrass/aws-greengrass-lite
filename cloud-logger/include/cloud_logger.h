// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef CLOUD_LOGGER_H
#define CLOUD_LOGGER_H

#include <ggl/vector.h>
int read_log_100(long index, GglObjVec *lists_obj, GglAlloc *alloc);

#endif
