/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_UTILS_H
#define GGL_UTILS_H

/*! Misc utilites */

#include <stdint.h>

/** Sleep for given duration in seconds.
 * Returns 0 on success else error code. */

int ggl_sleep(int64_t seconds);

#endif
