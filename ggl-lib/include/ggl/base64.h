// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BASE64_H
#define GGL_BASE64_H

//! Base64 utilities

#include "ggl/object.h"
#include <stdbool.h>

/// Convert a base64 buffer to its decoded data in place.
bool ggl_base64_decode_in_place(GglBuffer *target);

#endif
