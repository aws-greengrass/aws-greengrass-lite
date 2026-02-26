// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_BINPATH_H
#define GGL_BINPATH_H

#include <gg/error.h>
#include <gg/types.h>
#include <gg/vector.h>

/// Extract directory path from argv[0]
/// @param[in] argv0 The argv[0] from main() as a buffer
/// @param[out] result GgByteVec to store the directory path
/// @return GG_ERR_OK on success, error code on failure
GgError ggl_binpath_get_dir(GgBuffer argv0, GgByteVec *result);

/// Parse binary path from argv[0] and append a name to create a new path
/// @param[in] argv0 The argv[0] from main() as a buffer
/// @param[in] name The name to append to the binary directory
/// @param[out] result GgByteVec to store the result path
/// @return GG_ERR_OK on success, error code on failure
GgError ggl_binpath_append_name(
    GgBuffer argv0, GgBuffer name, GgByteVec *result
);

#endif
