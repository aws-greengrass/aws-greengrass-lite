// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_UTILS_H
#define GGL_UTILS_H

//! Misc utilities

#include "ggl/error.h"
#include <stdint.h>

/// Sleep for given duration in seconds.
GglError ggl_sleep(int64_t seconds);

/// Sleep for given duration in milliseconds.
GglError ggl_sleep_ms(int64_t ms);

#endif
