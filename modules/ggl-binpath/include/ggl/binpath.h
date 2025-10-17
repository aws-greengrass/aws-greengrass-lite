// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BINPATH_H
#define GGL_BINPATH_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/vector.h>

/// Extract directory path from argv[0]
/// @param[in] argv0 The argv[0] from main() as a buffer
/// @param[out] result GglByteVec to store the directory path
/// @return GGL_ERR_OK on success, error code on failure
GglError ggl_binpath_get_dir(GglBuffer argv0, GglByteVec *result);

/// Parse binary path from argv[0] and append a name to create a new path
/// @param[in] argv0 The argv[0] from main() as a buffer
/// @param[in] name The name to append to the binary directory
/// @param[out] result GglByteVec to store the result path
/// @return GGL_ERR_OK on success, error code on failure
GglError ggl_binpath_append_name(
    GglBuffer argv0, GglBuffer name, GglByteVec *result
);

#endif
