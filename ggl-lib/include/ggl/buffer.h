/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_BUFFER_H
#define GGL_BUFFER_H

/*! Map utilities */

#include "object.h"
#include <stdbool.h>

/** Get the value corresponding with a key.
 * If not found, returns false and `result` is NULL. */
bool ggl_buffer_eq(GglBuffer buf1, GglBuffer buf2);

#endif
