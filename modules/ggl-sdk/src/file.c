// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>

static void empty_sig_handler(int sig) {
    (void) sig;
}

// Lowest allowed priority in order to run before threads are created.
__attribute__((constructor(101))) static void ignore_sigpipe(void) {
    // If SIGPIPE is not blocked, writing to a socket that the server has closed
    // will result in this process being killed.
    // SIG_IGN should not be set as it is inherited across exec.
    // Since only SIG_IGN or SIG_DFL is inherited, and a handler set to a
    // function is reset to SIG_DFL after exec, we can start children with the
    // same settings this process was started with by only setting the handler
    // if the initial value is SIG_DFL.
    struct sigaction sa;
    int ret = sigaction(SIGPIPE, NULL, &sa);
    assert(ret == 0);
    if (sa.sa_handler == SIG_DFL) {
        sa = (struct sigaction) {
            .sa_handler = empty_sig_handler,
        };
        ret = sigaction(SIGPIPE, &sa, NULL);
        assert(ret == 0);
    }
}

static char path_comp_buf[NAME_MAX + 1];
static pthread_mutex_t path_comp_buf_mtx = PTHREAD_MUTEX_INITIALIZER;

GglError ggl_close(int fd) {
    // Do not loop on EINTR
    // Posix states that after an interrupted close, the state of the file
    // descriptor is unspecified. On Linux and most other systems, the fd is
    // released even if close failed with EINTR.

    sigset_t set;
    sigfillset(&set);
    sigset_t old_set;

    pthread_sigmask(SIG_SETMASK, &set, &old_set);

    int ret = close(fd);
    int err = errno;

    pthread_sigmask(SIG_SETMASK, &old_set, NULL);

    if ((ret == 0) || (err == EINTR)) {
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

static GglError ggl_openat(
    int dirfd, const char *pathname, int flags, mode_t mode, int *out
) {
    int ret;
    do {
        ret = openat(dirfd, pathname, flags, mode);
    } while ((ret < 0) && (errno == EINTR));
    if (ret < 0) {
        return GGL_ERR_FAILURE;
    }
    *out = ret;
    return GGL_ERR_OK;
}

static GglError copy_dir_fd(int dirfd, int flags, int *new) {
    int fd = openat_wrapper(dirfd, ".", O_CLOEXEC | O_DIRECTORY | flags, 0);
    if (fd < 0) {
        GGL_LOGE("Err %d while opening path.", errno);
        return GGL_ERR_FAILURE;
    }
    *new = fd;
    return GGL_ERR_OK;
}

static GglError ggl_mkdirat(int dirfd, const char *pathname, mode_t mode) {
    int sys_ret;
    int parent_fd;
    GglError ret = copy_dir_fd(dirfd, O_RDONLY, &parent_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP(cleanup_close, parent_fd);

    do {
        sys_ret = mkdirat(parent_fd, pathname, mode);
    } while ((sys_ret != 0) && (errno == EINTR));
    if (sys_ret != 0) {
        return GGL_ERR_FAILURE;
    }

    ret = ggl_fsync(parent_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return GGL_ERR_OK;
}

/// Open a directory, creating it if needed
/// dirfd must not be O_PATH
static GglError ggl_dir_openat_mkdir(
    int dirfd, const char *pathname, int flags, mode_t mode, int *out
) {
    int fd;
    GglError ret = ggl_openat(dirfd, pathname, flags, 0, &fd);
    if (ret != GGL_ERR_OK) {
        if (errno == ENOENT) {
            ret = ggl_mkdirat(dirfd, pathname, mode);
            if (ret != GGL_ERR_OK) {
                return ret;
            }
            ret = ggl_openat(dirfd, pathname, flags, 0, &fd);
            if (ret != GGL_ERR_OK) {
                return ret;
            }

            *out = fd;
            return GGL_ERR_OK;
        }
        return ret;
    }

    *out = fd;
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

/// Get fd for an absolute path to a dir
GglError ggl_dir_open(GglBuffer path, int flags, bool create, int *fd) {
    if (path.len == 0) {
        return GGL_ERR_INVALID;
    }

    bool absolute = false;
    GglBuffer rel_path = path;

    if (path.data[0] == '/') {
        absolute = true;
        rel_path = ggl_buffer_substr(path, 1, SIZE_MAX);
    }

    // Handle cases like `////`
    strip_trailing_slashes(&rel_path);

    if (rel_path.len == 0) {
        if (!absolute) {
            return GGL_ERR_INVALID;
        }
        // Path is `/`
        *fd = open("/", O_CLOEXEC | O_DIRECTORY | flags);
        if (*fd < 0) {
            GGL_LOGE("Err %d while opening /", errno);
            return GGL_ERR_FAILURE;
        }
        return GGL_ERR_OK;
    }

    int base_fd = open(
        absolute ? "/" : ".",
        O_CLOEXEC | O_DIRECTORY | (create ? O_RDONLY : O_PATH)
    );
    if (base_fd < 0) {
        GGL_LOGE("Err %d while opening /", errno);
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, base_fd);
    return ggl_dir_openat(base_fd, rel_path, flags, create, fd);
}

GglError ggl_dir_openat(
    int dirfd, GglBuffer path, int flags, bool create, int *fd
) {
    GglBuffer comp = GGL_STR("");
    GglBuffer rest = path;
    // Stripping trailing slashes is fine as we are assuming its a directory
    // regardless of trailing slash
    strip_trailing_slashes(&rest);

    // Opening the dir one parent at a time, so only need a buffer to store a
    // single path component for null termination

    // Make a copy of dirfd, so we can close it
    int cur_fd;
    GglError ret = copy_dir_fd(dirfd, O_PATH, &cur_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP_ID(cur_fd_cleanup, cleanup_close, cur_fd);

    GGL_MTX_SCOPE_GUARD(&path_comp_buf_mtx);

    // Iterate over parents from /
    while (split_path_first_comp(rest, &comp, &rest)) {
        // `/a//b` should be handled as `/a/b`
        if (comp.len == 0) {
            continue;
        }
        if (comp.len > NAME_MAX) {
            return GGL_ERR_RANGE;
        }
        memcpy(path_comp_buf, comp.data, comp.len);
        path_comp_buf[comp.len] = '\0';

        // Get next parent
        int new_fd;
        if (create) {
            ret = ggl_dir_openat_mkdir(
                cur_fd,
                path_comp_buf,
                O_CLOEXEC | O_DIRECTORY | O_PATH,
                0755,
                &new_fd
            );
        } else {
            ret = ggl_openat(
                cur_fd,
                path_comp_buf,
                O_CLOEXEC | O_DIRECTORY | O_PATH,
                0,
                &new_fd
            );
        }
        if (ret != GGL_ERR_OK) {
            GGL_LOGD("Err %d while opening path: %s", errno, path_comp_buf);
            return GGL_ERR_FAILURE;
        }

        // swap cur_fd
        (void) ggl_close(cur_fd);
        cur_fd = new_fd;
        // NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) false positive
        cur_fd_cleanup = new_fd;
    }

    // Handle final path component (non-empty since trailing slashes stripped)

    if (comp.len > NAME_MAX) {
        return GGL_ERR_NOMEM;
    }
    memcpy(path_comp_buf, comp.data, comp.len);
    path_comp_buf[comp.len] = '\0';

    int result;
    if (create) {
        ret = ggl_dir_openat_mkdir(
            cur_fd,
            path_comp_buf,
            O_CLOEXEC | O_DIRECTORY | flags,
            0755,
            &result
        );
    } else {
        ret = ggl_openat(
            cur_fd, path_comp_buf, O_CLOEXEC | O_DIRECTORY | flags, 0, &result
        );
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGD("Err %d while opening path: %s", errno, path_comp_buf);
        return GGL_ERR_FAILURE;
    }

    *fd = result;
    return GGL_ERR_OK;
}

GglError ggl_file_openat(
    int dirfd, GglBuffer path, int flags, mode_t mode, int *fd
) {
    int cur_fd;
    GglBuffer file = path;
    GglBuffer dir;
    if (split_path_last_comp(path, &dir, &file)) {
        bool create = (flags & O_CREAT) > 0;
        GglError ret = ggl_dir_openat(dirfd, dir, O_PATH, create, &cur_fd);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        // Make a copy of dirfd, so we can close it
        GglError ret = copy_dir_fd(dirfd, O_PATH, &cur_fd);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    GGL_CLEANUP(cleanup_close, cur_fd);

    if (file.len > NAME_MAX) {
        return GGL_ERR_NOMEM;
    }

    GGL_MTX_SCOPE_GUARD(&path_comp_buf_mtx);

    memcpy(path_comp_buf, file.data, file.len);
    path_comp_buf[file.len] = '\0';

    int result;
    GglError ret
        = ggl_openat(cur_fd, path_comp_buf, O_CLOEXEC | flags, mode, &result);
    if (ret != GGL_ERR_OK) {
        GGL_LOGD("Err %d while opening file: %s", errno, path_comp_buf);
        return GGL_ERR_FAILURE;
    }

    *fd = result;
    return GGL_ERR_OK;
}

/// Open a file.
GglError ggl_file_open(GglBuffer path, int flags, mode_t mode, int *fd) {
    if (path.len == 0) {
        return GGL_ERR_INVALID;
    }

    bool absolute = false;
    GglBuffer rel_path = path;

    if (path.data[0] == '/') {
        absolute = true;
        rel_path = ggl_buffer_substr(path, 1, SIZE_MAX);
    }

    if (rel_path.len == 0) {
        return GGL_ERR_INVALID;
    }

    int base_fd = open(absolute ? "/" : ".", O_CLOEXEC | O_DIRECTORY | O_PATH);
    if (base_fd < 0) {
        GGL_LOGE("Err %d while opening /", errno);
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, base_fd);
    return ggl_file_openat(base_fd, rel_path, flags, mode, fd);
}

GglError ggl_file_read_partial(int fd, GglBuffer *buf) {
    ssize_t ret = read(fd, buf->data, buf->len);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_RETRY;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            GGL_LOGE("Read timed out on fd %d.", fd);
            return GGL_ERR_FAILURE;
        }
        GGL_LOGE("Failed to read fd %d: %d.", fd, errno);
        return GGL_ERR_FAILURE;
    }
    if (ret == 0) {
        return GGL_ERR_NODATA;
    }

    *buf = ggl_buffer_substr(*buf, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_file_read(int fd, GglBuffer *buf) {
    GglBuffer rest = *buf;

    while (rest.len > 0) {
        GglError ret = ggl_file_read_partial(fd, &rest);
        if (ret == GGL_ERR_NODATA) {
            buf->len = (size_t) (rest.data - buf->data);
            return GGL_ERR_OK;
        }
        if (ret == GGL_ERR_RETRY) {
            continue;
        }
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_file_read_exact(int fd, GglBuffer buf) {
    GglBuffer copy = buf;
    GglError ret = ggl_file_read(fd, &copy);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return buf.len == copy.len ? GGL_ERR_OK : GGL_ERR_NODATA;
}

GglError ggl_file_write_partial(int fd, GglBuffer *buf) {
    ssize_t ret = write(fd, buf->data, buf->len);
    if (ret < 0) {
        if (errno == EINTR) {
            return GGL_ERR_RETRY;
        }
        if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
            GGL_LOGE("Write timed out on fd %d.", fd);
            return GGL_ERR_FAILURE;
        }
        if (errno == EPIPE) {
            GGL_LOGE("Write failed to %d; peer closed socket/pipe.", fd);
            return GGL_ERR_NOCONN;
        }
        GGL_LOGE("Failed to write to fd %d: %d.", fd, errno);
        return GGL_ERR_FAILURE;
    }

    *buf = ggl_buffer_substr(*buf, (size_t) ret, SIZE_MAX);
    return GGL_ERR_OK;
}

GglError ggl_file_write(int fd, GglBuffer buf) {
    GglBuffer rest = buf;

    while (rest.len > 0) {
        GglError ret = ggl_file_write_partial(fd, &rest);
        if (ret == GGL_ERR_RETRY) {
            continue;
        }
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return GGL_ERR_OK;
}

GglError ggl_file_read_path_at(int dirfd, GglBuffer path, GglBuffer *content) {
    GglBuffer buf = *content;
    int fd;
    GglError ret = ggl_file_openat(dirfd, path, O_RDONLY, 0, &fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    GGL_CLEANUP(cleanup_close, fd);

    struct stat info;
    int sys_ret = fstat(fd, &info);
    if (sys_ret != 0) {
        GGL_LOGE(
            "Err %d while calling fstat on file: %.*s",
            errno,
            (int) path.len,
            path.data
        );
        return GGL_ERR_FAILURE;
    }

    size_t file_size = (size_t) info.st_size;

    if (file_size > buf.len) {
        GGL_LOGE(
            "Insufficient memory for file %.*s.", (int) path.len, path.data
        );
        return GGL_ERR_NOMEM;
    }

    buf.len = file_size;

    ret = ggl_file_read_exact(fd, buf);
    if (ret == GGL_ERR_OK) {
        *content = buf;
    }
    return ret;
}

GglError ggl_file_read_path(GglBuffer path, GglBuffer *content) {
    if (path.len == 0) {
        return GGL_ERR_INVALID;
    }

    bool absolute = false;
    GglBuffer rel_path = path;

    if (path.data[0] == '/') {
        absolute = true;
        rel_path = ggl_buffer_substr(path, 1, SIZE_MAX);
    }

    if (rel_path.len == 0) {
        return GGL_ERR_INVALID;
    }

    int base_fd = open(absolute ? "/" : ".", O_CLOEXEC | O_DIRECTORY | O_PATH);
    if (base_fd < 0) {
        GGL_LOGE("Err %d while opening /", errno);
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, base_fd);

    GglError ret = ggl_file_read_path_at(base_fd, rel_path, content);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Err %d occurred while reading file %.*s",
            errno,
            (int) rel_path.len,
            rel_path.data
        );
        return ret;
    }

    return GGL_ERR_OK;
}
