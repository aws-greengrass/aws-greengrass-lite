/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_MATH_H
#define GGL_MATH_H

/*! Math utilites */

#include <stdint.h>

/** Absolute value, avoiding undefined behavior.
 * i.e. avoiding -INT64_MIN for twos-compliment) */
uint64_t ggl_abs(int64_t i64);

#endif
