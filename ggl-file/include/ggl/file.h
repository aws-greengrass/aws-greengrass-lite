// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FILE_H
#define GGL_FILE_H

//! File system functionality

#include <sys/types.h>
#include <ggl/error.h>
#include <ggl/object.h>

/// Open a directory, creating it if needed
GglError ggl_dir_open(GglBuffer path, int flags, int *fd);

/// Open a directory under dirfd, creating it if needed
GglError ggl_dir_openat(int dirfd, GglBuffer path, int flags, int *fd);

/// Open a file under dirfd
GglError ggl_file_openat(
    int dirfd, GglBuffer path, int flags, mode_t mode, int *fd
);

/// Copy directory contents recursively
GglError ggl_copy_dir(int source_fd, int dest_fd);

#endif
