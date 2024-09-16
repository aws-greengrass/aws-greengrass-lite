// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FILE_H
#define GGL_FILE_H

//! File system functionality

#include <sys/types.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdbool.h>

/// Call close on a fd, handling EINTR
GglError ggl_close(int fd);

/// Enable defer for closing file descriptors.
GGL_DEFINE_DEFER(ggl_close, int, fd, if (*fd >= 0) ggl_close(*fd))

/// Call fsync on an file/dir, handling EINTR
GglError ggl_fsync(int fd);

/// Open a directory, creating it if create is set.
GglError ggl_dir_open(GglBuffer path, int flags, bool create, int *fd);

/// Open a directory under dirfd, creating it if needed
/// If create is true tries calling mkdir for missing dirs, and dirfd must not
/// be O_PATH.
GglError ggl_dir_openat(
    int dirfd, GglBuffer path, int flags, bool create, int *fd
);

/// Open a file under dirfd
GglError ggl_file_openat(
    int dirfd, GglBuffer path, int flags, mode_t mode, int *fd
);

/// Open a file.
GglError ggl_file_open(GglBuffer path, int flags, mode_t mode, int *fd);

/// Copy directory contents recursively
GglError ggl_copy_dir(int source_fd, int dest_fd);

#endif
