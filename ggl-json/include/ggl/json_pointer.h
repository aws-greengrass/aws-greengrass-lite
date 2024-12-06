// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_JSON_POINTER_H
#define GGL_JSON_POINTER_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/vector.h>

// Parse a json pointer buffer into a list of keys
GglError ggl_gg_config_jsonp_parse(GglBuffer json_ptr, GglBufVec *key_path);

#endif
