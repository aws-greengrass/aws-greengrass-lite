// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/math.h"
#include <stdint.h>

uint64_t ggl_abs(int64_t i64) {
    if (i64 >= 0) {
        return (uint64_t) i64;
    }
    if (i64 >= INT32_MIN) {
        // Handling small values so next step can divide without 0 result
        return (uint64_t) -i64;
    }
    // Avoiding assuming twos-compliment.
    // Using division to split into two negatable values and recombine. */
    return ((uint64_t) (i64 / INT32_MIN) * (uint64_t) (-(int64_t) INT32_MIN))
        + (uint64_t) (-(i64 % INT32_MIN));
}
