// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FILE_H
#define GGL_FILE_H

//! File system functionality

#include <sys/types.h>
#include <dirent.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdbool.h>
#include <stdio.h>

// TODO: Remove after ggl-file refactor
// IWYU pragma: always_keep (IWYU has trouble with cleanup_close)

/// Call close on a fd, handling EINTR
GglError ggl_close(int fd);

/// Cleanup function for closing file descriptors.
static inline void cleanup_close(const int *fd) {
    if (*fd >= 0) {
        ggl_close(*fd);
    }
}

/// Cleanup function for closing C file handles (e.g. opened by fdopen)
static inline void cleanup_fclose(FILE **fp) {
    if (*fp != NULL) {
        fclose(*fp);
    }
}

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

/// Open a file
GglError ggl_file_open(GglBuffer path, int flags, mode_t mode, int *fd);

/// Read from file.
/// If result buf value is less than the input size, the file has ended.
GglError ggl_file_read(int fd, GglBuffer *buf);

/// Read from file, returning error if file ends before buf is full.
GglError ggl_file_read_exact(int fd, GglBuffer buf);

/// Read portion of data from file (makes single read call).
/// Returns remaining buffer.
/// Caller must handle GGL_ERR_RETRY and GGL_ERR_NODATA
GglError ggl_file_read_partial(int fd, GglBuffer *buf);

/// Write buffer to file.
GglError ggl_file_write(int fd, GglBuffer buf);

/// Write portion of buffer to file (makes single write call).
/// Returns remaining buffer.
/// Caller must handle GGL_ERR_RETRY
GglError ggl_file_write_partial(int fd, GglBuffer *buf);

/// Read file contents from path
GglError ggl_file_read_path(GglBuffer path, GglBuffer *content);

/// Read file contents from path under dirfd
GglError ggl_file_read_path_at(int dirfd, GglBuffer path, GglBuffer *content);

/// Copy directory contents recursively
GglError ggl_copy_dir(int source_fd, int dest_fd);

static inline void cleanup_closedir(DIR **dirp) {
    if (*dirp != NULL) {
        closedir(*dirp);
    }
}

#endif
