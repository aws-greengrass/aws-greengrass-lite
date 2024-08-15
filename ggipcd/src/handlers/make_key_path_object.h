// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef MAKE_KEY_PATH_OBJECT_H
#define MAKE_KEY_PATH_OBJECT_H

#include <ggl/object.h>

#define MAXIMUM_KEY_PATH_DEPTH 100

/// @brief Combine the component name and key path and return a new key path
/// @param component_name_object The component name to add to the key path
/// @param key_path_object  the key path to modify
/// @return a new key path that includes the component name
GglObject *make_key_path_object(
    GglObject *component_name_object, GglObject *key_path_object
);

#endif
