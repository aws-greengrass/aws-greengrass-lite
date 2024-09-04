// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/process.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <limits.h>
#include <linux/close_range.h>
#include <linux/sched.h>
#include <poll.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static pid_t sys_clone3(struct clone_args *args) {
    return (pid_t) syscall(SYS_clone3, args, sizeof(struct clone_args));
}

static int sys_pidfd_send_signal(
    int pidfd, int sig, siginfo_t *info, unsigned int flags
) {
    return (int) syscall(SYS_pidfd_send_signal, pidfd, sig, info, flags);
}

GglError ggl_process_spawn(char *const argv[], int *handle) {
    assert(argv[0] != NULL);
    assert(handle != NULL);

    int pidfd = -1;
    struct clone_args args = {
        .pidfd = (uintptr_t) &pidfd,
        .flags = CLONE_PIDFD | CLONE_CLEAR_SIGHAND,
        .exit_signal = SIGCHLD,
    };

    pid_t pid = sys_clone3(&args);

    if (pid == 0) {
        close_range(3, UINT_MAX, CLOSE_RANGE_UNSHARE);

        execvp(argv[0], argv);

        _Exit(1);
    }

    if (pid < 0) {
        GGL_LOGE("process", "Err %d when calling clone3.", errno);
        return GGL_ERR_FAILURE;
    }

    if (pidfd < 0) {
        // probably out of file descriptors?
        GGL_LOGE("process", "Failed to obtain child pidfd.");
        // Child leaks
        return GGL_ERR_FAILURE;
    }

    *handle = pidfd;
    return GGL_ERR_OK;
}

GglError ggl_process_wait(int handle, bool *exit_status) {
    while (true) {
        siginfo_t info = { 0 };
        int ret = waitid(P_PIDFD, (id_t) handle, &info, WEXITED);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            GGL_LOGE("process", "Err %d when calling waitid.", errno);
            return GGL_ERR_FAILURE;
        }

        switch (info.si_code) {
        case CLD_EXITED:
            if (exit_status != NULL) {
                *exit_status = info.si_status == 0;
            }
            return GGL_ERR_OK;
        case CLD_KILLED:
        case CLD_DUMPED:
            if (exit_status != NULL) {
                *exit_status = false;
            }
            return GGL_ERR_OK;
        default:;
        }
    }
}

static int ms_remaining(
    struct timespec start, struct timespec now, int ms_requested
) {
    time_t sec_elapsed = now.tv_sec - start.tv_sec;
    suseconds_t us_elapsed = now.tv_nsec - start.tv_nsec;
    uint64_t ms_elapsed = (uint64_t) (sec_elapsed * 1000 + us_elapsed / 1000);
    if (ms_elapsed > (uint64_t) ms_requested) {
        return 0;
    }
    uint64_t ms_remaining = (uint64_t) ms_requested - ms_elapsed;
    return (ms_remaining < INT32_MAX) ? (int) ms_remaining : INT32_MAX;
}

static GglError poll_wrapper(int handle, uint32_t timeout) {
    assert(timeout <= INT32_MAX / 1000);

    struct timespec start = { 0 };
    int ret = clock_gettime(CLOCK_MONOTONIC, &start);
    if (ret < 0) {
        GGL_LOGE("process", "Err %d when calling clock_gettime.", errno);
        return GGL_ERR_FAILURE;
    }

    int ms_timeout = (int) timeout * 1000;

    struct pollfd poll_handle = { .fd = handle, .events = POLLIN };

    while (true) {
        ret = poll(&poll_handle, 1, ms_timeout);
        if (ret < 0) {
            if (errno == EINTR) {
                struct timespec now;
                ret = clock_gettime(CLOCK_MONOTONIC, &now);
                if (ret < 0) {
                    GGL_LOGE(
                        "process", "Err %d when calling clock_gettime.", errno
                    );
                    return GGL_ERR_FAILURE;
                }
                ms_timeout = ms_remaining(start, now, ms_timeout);
                start = now;
                continue;
            }

            // Error
            GGL_LOGE("process", "Err %d when calling poll.", errno);
            return GGL_ERR_FAILURE;
        }
        if (ret > 0) {
            return GGL_ERR_OK;
        }
        // Timed out
        return GGL_ERR_RETRY;
    }
}

GglError ggl_process_kill(int handle, uint32_t term_timeout) {
    if (term_timeout == 0) {
        int sys_ret = sys_pidfd_send_signal(handle, SIGKILL, NULL, 0);
        if (sys_ret < 0) {
            return GGL_ERR_FAILURE;
        }
    } else {
        int sys_ret = sys_pidfd_send_signal(handle, SIGTERM, NULL, 0);
        if (sys_ret < 0) {
            return GGL_ERR_FAILURE;
        }

        GglError ret = poll_wrapper(handle, term_timeout);
        if (ret != GGL_ERR_OK) {
            if (ret != GGL_ERR_RETRY) {
                return ret;
            }

            sys_ret = sys_pidfd_send_signal(handle, SIGKILL, NULL, 0);
            if (sys_ret < 0) {
                return GGL_ERR_FAILURE;
            }
        }
    }

    return ggl_process_wait(handle, NULL);
}
