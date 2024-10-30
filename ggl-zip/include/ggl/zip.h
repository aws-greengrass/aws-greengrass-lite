// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_ZIP_H
#define GGL_ZIP_H

//! Zip file functionality

#include <sys/types.h>
#include <ggl/buffer.h>
#include <ggl/error.h>

/// Unarchive all entries from the zip file in a directory to the destination
/// directory. All created, uncompressed files use the given mode.
GglError ggl_zip_unarchive(
    int source_dest_dir_fd, GglBuffer zip_path, int dest_dir_fd, mode_t mode
);

#endif
