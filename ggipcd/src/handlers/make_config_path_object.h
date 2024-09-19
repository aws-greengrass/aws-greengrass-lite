// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef MAKE_CONFIG_PATH_OBJECT_H
#define MAKE_CONFIG_PATH_OBJECT_H

#include <ggl/object.h>

#define MAXIMUM_KEY_PATH_DEPTH 100

/// @brief Combine the component name and key path and return a new configuration path
/// @param component_name_object The component name
/// @param key_path_object  the key path within the component configuration
/// @return a configuration path to the given key path
GglObject *ggl_make_config_path_object(
    GglObject *component_name_object, GglObject *key_path_object
);

#endif
