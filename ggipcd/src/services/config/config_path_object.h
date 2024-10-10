// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef MAKE_CONFIG_PATH_OBJECT_H
#define MAKE_CONFIG_PATH_OBJECT_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

/// Combine the component name and key path and returns a new configuration path
/// result uses static memory owned by this function which is valid until the
/// next call. Not re-entrant.
GglError ggl_make_config_path_object(
    GglBuffer component_name, GglList key_path, GglBufList *result
);

/// Parse the component name and key path from a configuration path
/// component_name will point to the data in config_path which contains
/// the component name.
/// key_path uses static memory owned by this function which is valid until the
/// next call. Not re-entrant.
GglError ggl_parse_config_path(
    GglList config_path, GglBuffer *component_name, GglList *key_path
);

#endif
