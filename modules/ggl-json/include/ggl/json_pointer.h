// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_JSON_POINTER_H
#define GGL_JSON_POINTER_H

#include <gg/error.h>
#include <gg/types.h>
#include <gg/vector.h>

// Parse a json pointer buffer into a list of keys
GgError ggl_gg_config_jsonp_parse(GgBuffer json_ptr, GgBufVec *key_path);

#endif
