/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_CONSTANTS_H
#define GGL_CONSTANTS_H

/// The max depth of nested objects via maps or lists
#define GGL_MAX_OBJECT_DEPTH 15

// According to
// https://docs.aws.amazon.com/iot/latest/apireference/API_ThingAttribute.html
#define THING_NAME_MAX_LENGTH 128

#endif
