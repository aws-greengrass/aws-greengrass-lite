/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_MACRO_UTIL_H
#define GGL_MACRO_UTIL_H

/** Expands to first argument. */
#define GGL_MACRO_FIRST(first, ...) first

/** Expands to args, skipping first. */
#define GGL_MACRO_REST(first, ...) __VA_ARGS__

/** Combine two args, expanding them. */
#define GGL_MACRO_PASTE(left, right) GGL_MACRO_PASTE2(left, right)
#define GGL_MACRO_PASTE2(left, right) left##right

#endif
