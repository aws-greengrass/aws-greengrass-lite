// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_SEMVER_H
#define GGL_SEMVER_H

#include <gg/types.h>
#include <stdbool.h>

bool is_in_range(GgBuffer version, GgBuffer requirements_range);

#endif
