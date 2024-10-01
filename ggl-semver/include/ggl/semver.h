// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_SEMVER_H
#define GGL_SEMVER_H

#include <ggl/object.h>
#include <stdbool.h>

bool is_contain(GglBuffer version, GglBuffer requirements_range);

#endif
