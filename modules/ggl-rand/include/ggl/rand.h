// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_RAND_H
#define GGL_RAND_H

//! Greengrass random util

#include <ggl/buffer.h>
#include <ggl/error.h>

GglError ggl_rand_fill(GglBuffer buf);

#endif
