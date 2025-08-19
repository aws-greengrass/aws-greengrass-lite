/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_NUCLEUS_CONSTANTS_H
#define GGL_NUCLEUS_CONSTANTS_H

/// Maximum number of generic components that can be tracked.
/// This is unique component names over all time, not just at a given moment.
/// Can be configured with `-DGGL_MAX_GENERIC_COMPONENTS=<N>`.
#ifndef GGL_MAX_GENERIC_COMPONENTS
#define GGL_MAX_GENERIC_COMPONENTS 50
#endif

// https://docs.aws.amazon.com/greengrass/v2/APIReference/API_DescribeComponent.html
#define GGL_COMPONENT_NAME_MAX_LEN 128

// https://docs.aws.amazon.com/general/latest/gr/greengrassv2.html#limits_greengrassv2
#define GGL_COMPONENT_RECIPE_MAX_LEN (16 * 1024)

#endif
