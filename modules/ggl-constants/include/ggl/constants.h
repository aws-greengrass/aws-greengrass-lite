/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_CONSTANTS_H
#define GGL_CONSTANTS_H

/// The maximum number of components supported
#define MAX_COMPONENTS 64

// According to
// https://docs.aws.amazon.com/iot/latest/apireference/API_ThingAttribute.html
#define THING_NAME_MAX_LENGTH 128

/// The max depth of nested objects via maps or lists
#define GGL_MAX_OBJECT_DEPTH 15

/// The maximum expected config keys (including nested) held under one component
/// configuration
#define MAX_CONFIG_DESCENDANTS_PER_COMPONENT 256

/// The maximum expected config keys held as children of a single config object
// TODO: Should be at least as big as MAX_COMPONENTS, add static assert?
#define MAX_CONFIG_CHILDREN_PER_OBJECT 64

/// Maximum length of generic component name.
#define MAX_COMPONENT_NAME_LENGTH (128)

#endif
