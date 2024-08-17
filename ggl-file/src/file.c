// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#define _GNU_SOURCE

#include "ggl/file.h"
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_PATH_COMPONENT_LENGTH 256

static char path_comp_buf[MAX_PATH_COMPONENT_LENGTH + 1];
static pthread_mutex_t path_comp_buf_mtx = PTHREAD_MUTEX_INITIALIZER;

GGL_DEFINE_DEFER(closedir, DIR *, dirp, if (*dirp != NULL) closedir(*dirp))

/// Call fsync, looping when interrupted by signal.
static int fsync_wrapper(int fd) {
    int ret;
    do {
        ret = fsync(fd);
    } while ((ret < 0) && (errno == EINTR));
    return ret;
}

/// Atomically copy a file (if source/dest on same fs).
static GglError copy_file(const char *name, int source_fd, int dest_fd) {
    pthread_mutex_lock(&path_comp_buf_mtx);
    GGL_DEFER(pthread_mutex_unlock, path_comp_buf_mtx);

    // For atomic writes, one must write to temp file and use rename which
    // atomically moves and replaces a file as long as the source and
    // destination are on the same filesystem.

    // For the same filesystem requirement, we make a temp file in the target
    // directory.
    // Prefixing the name with `.~` to make it hidden and clear its a temp file.
    // TODO: Check for filesystem O_TMPFILE support and use that instead.
    size_t name_len = strlen(name);
    if (name_len > MAX_PATH_COMPONENT_LENGTH - 2) {
        return GGL_ERR_NOMEM;
    }
    memcpy(path_comp_buf, ".~", 2);
    memcpy(&path_comp_buf[2], name, name_len);
    path_comp_buf[name_len + 2] = '\0';

    // Open file in source dir
    int old_fd = openat(source_fd, name, O_CLOEXEC | O_RDONLY);
    if (old_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening %s.", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, old_fd);

    // Open target temp file
    int new_fd = openat(
        dest_fd,
        path_comp_buf,
        O_CLOEXEC | O_WRONLY | O_TRUNC | O_CREAT,
        S_IRWXU
    );
    if (new_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening %s.", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, new_fd);

    struct stat stat;
    if (fstat(old_fd, &stat) != 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while calling fstat on %s.", err, name);
        return GGL_ERR_FAILURE;
    }

    // Using copy_file_range does all of the copying in the kernel, and enables
    // use of file system acceleration like reflinks, which may allow making a
    // CoW copy without duplicating data.
    ssize_t copy_ret;
    do {
        copy_ret = copy_file_range(
            old_fd, NULL, new_fd, NULL, (size_t) stat.st_size, 0
        );
    } while (copy_ret > 0);
    if (copy_ret < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while copying %s.", err, name);
        return GGL_ERR_FAILURE;
    }

    // If we call rename without first calling fsync, the data may not be
    // flushed, and system interruption could result in a corrupted target file
    (void) fsync_wrapper(new_fd);

    GGL_DEFER_FORCE(new_fd);

    // Perform the rename to the target location
    int ret_int = renameat(dest_fd, path_comp_buf, dest_fd, name);
    if (ret_int < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while moving %s.", err, name);
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

/// Splits buffer to part before first `/` and part after
static bool split_path_first_comp(
    GglBuffer path, GglBuffer *comp, GglBuffer *rest
) {
    for (size_t i = 0; i < path.len; i++) {
        if (path.data[i] == '/') {
            *comp = ggl_buffer_substr(path, 0, i);
            *rest = ggl_buffer_substr(path, i + 1, SIZE_MAX);
            return true;
        }
    }

    *comp = path;
    *rest = ggl_buffer_substr(path, path.len, SIZE_MAX);
    return false;
}

static void strip_trailing_slashes(GglBuffer *path) {
    while ((path->len >= 1) && (path->data[path->len - 1] == '/')) {
        path->len -= 1;
    }
}

/// Open a directory, creating it if needed
static int open_or_mkdir_at(int dirfd, const char *pathname, int flags) {
    (void) mkdirat(dirfd, pathname, 0700);
    return openat(dirfd, pathname, flags);
}

/// Get fd for an absolute path to a dir
GglError ggl_dir_open(GglBuffer path, int flags, int *fd) {
    if (path.len == 0) {
        return GGL_ERR_INVALID;
    }
    if (path.data[0] != '/') {
        return GGL_ERR_UNSUPPORTED;
    }

    GglBuffer rest = ggl_buffer_substr(path, 1, SIZE_MAX);
    // Handle cases like `////`
    strip_trailing_slashes(&rest);

    if (rest.len == 0) {
        // Path is `/`
        *fd = open("/", O_CLOEXEC | O_DIRECTORY | flags);
        if (*fd < 0) {
            int err = errno;
            GGL_LOGE(
                "file",
                "Err %d while opening path: %.*s",
                err,
                (int) path.len,
                path.data
            );
            return GGL_ERR_FAILURE;
        }
        return GGL_ERR_OK;
    }

    int root_fd = open("/", O_CLOEXEC | O_DIRECTORY | O_PATH);
    if (root_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening /", err);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, root_fd);
    return ggl_dir_openat(root_fd, rest, flags, fd);
}

GglError ggl_dir_openat(int dirfd, GglBuffer path, int flags, int *fd) {
    GglBuffer comp = GGL_STR("");
    GglBuffer rest = path;
    // Stripping trailing slashes is fine as we are assuming its a directory
    // regardless of trailing slash
    strip_trailing_slashes(&rest);

    // Opening the dir one parent at a time, so only need a buffer to store a
    // single path component for null termination

    // Make a copy of dirfd, so we can close it
    int cur_fd = openat(dirfd, ".", O_CLOEXEC | O_DIRECTORY | O_PATH);
    if (cur_fd < 0) {
        int err = errno;
        GGL_LOGE(
            "file",
            "Err %d while opening path: %.*s",
            err,
            (int) path.len,
            path.data
        );
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, cur_fd);

    pthread_mutex_lock(&path_comp_buf_mtx);
    GGL_DEFER(pthread_mutex_unlock, path_comp_buf_mtx);

    // Iterate over parents from /
    while (split_path_first_comp(rest, &comp, &rest)) {
        // `/a//b` should be handled as `/a/b`
        if (comp.len == 0) {
            continue;
        }
        if (comp.len > MAX_PATH_COMPONENT_LENGTH) {
            return GGL_ERR_NOMEM;
        }
        memcpy(path_comp_buf, comp.data, comp.len);
        path_comp_buf[comp.len] = '\0';

        // Get next parent
        int new_fd = open_or_mkdir_at(
            cur_fd, path_comp_buf, O_CLOEXEC | O_DIRECTORY | O_PATH
        );
        if (new_fd < 0) {
            int err = errno;
            GGL_LOGE(
                "file", "Err %d while opening path: %s", err, path_comp_buf
            );
            return GGL_ERR_FAILURE;
        }

        // swap cur_fd
        close(cur_fd);
        cur_fd = new_fd;
    }

    // Handle final path component (non-empty since trailing slashes stripped)

    if (comp.len > MAX_PATH_COMPONENT_LENGTH) {
        return GGL_ERR_NOMEM;
    }
    memcpy(path_comp_buf, comp.data, comp.len);
    path_comp_buf[comp.len] = '\0';

    *fd = open_or_mkdir_at(
        cur_fd, path_comp_buf, O_CLOEXEC | O_DIRECTORY | flags
    );
    if (*fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening path: %s", err, path_comp_buf);
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

/// Recursively copy a subdirectory.
// NOLINTNEXTLINE(misc-no-recursion)
static GglError copy_dir(const char *name, int source_fd, int dest_fd) {
    int source_subdir_fd
        = openat(source_fd, name, O_CLOEXEC | O_DIRECTORY | O_RDONLY);
    if (source_subdir_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening dir: %s", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, source_subdir_fd);

    int dest_subdir_fd
        = open_or_mkdir_at(dest_fd, name, O_CLOEXEC | O_DIRECTORY | O_RDONLY);
    if (dest_subdir_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening dir: %s", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(close, dest_subdir_fd);

    return ggl_copy_dir(source_subdir_fd, dest_subdir_fd);
}

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_copy_dir(int source_fd, int dest_fd) {
    // We need to copy source_fd as fdopendir takes ownership of it
    int source_fd_copy
        = openat(source_fd, ".", O_CLOEXEC | O_DIRECTORY | O_RDONLY);
    if (source_fd_copy < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening dir.", err);
        return GGL_ERR_FAILURE;
    }

    DIR *source_dir = fdopendir(source_fd_copy);
    if (source_dir == NULL) {
        GGL_LOGE("file", "Failed to open dir.");
        close(source_fd_copy);
        return GGL_ERR_FAILURE;
    }
    // Also closes source_fd_copy
    GGL_DEFER(closedir, source_dir);

    while (true) {
        // Directory stream is not shared between threads.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        struct dirent *entry = readdir(source_dir);
        if (entry == NULL) {
            break;
        }

        if (entry->d_type == DT_DIR) {
            if (strcmp(entry->d_name, ".") == 0) {
                continue;
            }
            if (strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            GglError ret = copy_dir(entry->d_name, source_fd, dest_fd);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        } else if (entry->d_type == DT_REG) {
            GglError ret = copy_file(entry->d_name, source_fd, dest_fd);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
        } else {
            GGL_LOGE("file", "Unexpected special file: %s", entry->d_name);
            return GGL_ERR_INVALID;
        }
    }

    // Flush directory entries to disk (Must not be O_PATH)
    (void) fsync_wrapper(dest_fd);

    return GGL_ERR_OK;
}
