// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

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
#include <sys/stat.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_PATH_COMPONENT_LENGTH 256

static char path_comp_buf[MAX_PATH_COMPONENT_LENGTH + 1];
static pthread_mutex_t path_comp_buf_mtx = PTHREAD_MUTEX_INITIALIZER;

GGL_DEFINE_DEFER(closedir, DIR *, dirp, if (*dirp != NULL) closedir(*dirp))

GglError ggl_close(int fd) {
    // Do not loop on EINTR
    // Posix states that after an interrupted close, the state of the file
    // descriptor is unspecified. On Linux and most other systems, the fd is
    // released even if close failed with EINTR.
    int ret = close(fd);
    if ((ret == 0) || (errno == EINTR)) {
        return GGL_ERR_OK;
    }
    return GGL_ERR_FAILURE;
}

GglError ggl_fsync(int fd) {
    int ret;
    do {
        ret = fsync(fd);
    } while ((ret != 0) && (errno == EINTR));
    return (ret == 0) ? GGL_ERR_OK : GGL_ERR_FAILURE;
}

/// Call openat, looping when interrupted by signal.
static int openat_wrapper(
    int dirfd, const char *pathname, int flags, mode_t mode
) {
    int ret;
    do {
        ret = openat(dirfd, pathname, flags, mode);
    } while ((ret < 0) && (errno == EINTR));
    return ret;
}

/// Atomically copy a file (if source/dest on same fs).
static GglError copy_file(const char *name, int source_fd, int dest_fd) {
    pthread_mutex_lock(&path_comp_buf_mtx);
    GGL_DEFER(pthread_mutex_unlock, path_comp_buf_mtx);

    // TODO: Ensure name has no path components

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
    int old_fd = openat_wrapper(source_fd, name, O_CLOEXEC | O_RDONLY, 0);
    if (old_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening %s.", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(ggl_close, old_fd);

    // Open target temp file
    int new_fd = openat_wrapper(
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
    GGL_DEFER(ggl_close, new_fd);

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
    GglError ret = ggl_fsync(new_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("file", "Err %d while fsync on %s.", errno, name);
        return ret;
    }

    GGL_DEFER_CANCEL(new_fd);
    ret = ggl_close(new_fd);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("file", "Err %d while closing %s.", errno, name);
        return ret;
    }

    // Perform the rename to the target location
    int err = renameat(dest_fd, path_comp_buf, dest_fd, name);
    if (err != 0) {
        GGL_LOGE("file", "Err %d while moving %s.", errno, name);
        return GGL_ERR_FAILURE;
    }

    // If this fails, file has been moved but failed to write inode for the
    // directory. In this case the file may be overwritten so returning a
    // failure could be more error-prone for the caller.
    (void) ggl_fsync(dest_fd);

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

/// Splits buffer to part before last `/` and part after
static bool split_path_last_comp(
    GglBuffer path, GglBuffer *prefix, GglBuffer *comp
) {
    for (size_t i = path.len; i > 0; i--) {
        if (path.data[i - 1] == '/') {
            *prefix = ggl_buffer_substr(path, 0, i - 1);
            *comp = ggl_buffer_substr(path, i, SIZE_MAX);
            return true;
        }
    }

    *comp = path;
    *prefix = ggl_buffer_substr(path, 0, 0);
    return false;
}

static void strip_trailing_slashes(GglBuffer *path) {
    while ((path->len >= 1) && (path->data[path->len - 1] == '/')) {
        path->len -= 1;
    }
}

/// Open a directory, creating it if needed
static int open_or_mkdir_at(int dirfd, const char *pathname, int flags) {
    int mkdir_ret = mkdirat(dirfd, pathname, 0700);
    int fd = openat_wrapper(dirfd, pathname, flags, 0);
    if ((mkdir_ret == 0) && (fd >= 0)) {
        // These fail for O_PATH
        (void) ggl_fsync(fd);
        (void) ggl_fsync(dirfd);
    }
    return fd;
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
    GGL_DEFER(ggl_close, root_fd);
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
    int cur_fd
        = openat_wrapper(dirfd, ".", O_CLOEXEC | O_DIRECTORY | O_PATH, 0);
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
    GGL_DEFER(ggl_close, cur_fd);

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
        ggl_close(cur_fd);
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

GglError ggl_file_openat(
    int dirfd, GglBuffer path, int flags, mode_t mode, int *fd
) {
    int cur_fd;
    GglBuffer file = path;
    GglBuffer dir;
    if (split_path_last_comp(path, &dir, &file)) {
        GglError ret = ggl_dir_openat(dirfd, dir, O_PATH, &cur_fd);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        // Make a copy of dirfd, so we can close it
        cur_fd
            = openat_wrapper(dirfd, ".", O_CLOEXEC | O_DIRECTORY | O_PATH, 0);
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
    }
    GGL_DEFER(ggl_close, cur_fd);

    if (file.len > MAX_PATH_COMPONENT_LENGTH) {
        return GGL_ERR_NOMEM;
    }

    pthread_mutex_lock(&path_comp_buf_mtx);
    GGL_DEFER(pthread_mutex_unlock, path_comp_buf_mtx);

    memcpy(path_comp_buf, file.data, file.len);
    path_comp_buf[file.len] = '\0';

    *fd = openat_wrapper(cur_fd, path_comp_buf, O_CLOEXEC | flags, mode);
    if (*fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening file: %s", err, path_comp_buf);
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

/// Recursively copy a subdirectory.
// NOLINTNEXTLINE(misc-no-recursion)
static GglError copy_dir(const char *name, int source_fd, int dest_fd) {
    int source_subdir_fd = openat_wrapper(
        source_fd, name, O_CLOEXEC | O_DIRECTORY | O_RDONLY, 0
    );
    if (source_subdir_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening dir: %s", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(ggl_close, source_subdir_fd);

    int dest_subdir_fd
        = open_or_mkdir_at(dest_fd, name, O_CLOEXEC | O_DIRECTORY | O_RDONLY);
    if (dest_subdir_fd < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening dir: %s", err, name);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(ggl_close, dest_subdir_fd);

    return ggl_copy_dir(source_subdir_fd, dest_subdir_fd);
}

// NOLINTNEXTLINE(misc-no-recursion)
GglError ggl_copy_dir(int source_fd, int dest_fd) {
    // We need to copy source_fd as fdopendir takes ownership of it
    int source_fd_copy
        = openat_wrapper(source_fd, ".", O_CLOEXEC | O_DIRECTORY | O_RDONLY, 0);
    if (source_fd_copy < 0) {
        int err = errno;
        GGL_LOGE("file", "Err %d while opening dir.", err);
        return GGL_ERR_FAILURE;
    }

    DIR *source_dir = fdopendir(source_fd_copy);
    if (source_dir == NULL) {
        GGL_LOGE("file", "Failed to open dir.");
        ggl_close(source_fd_copy);
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
    (void) ggl_fsync(dest_fd);

    return GGL_ERR_OK;
}
