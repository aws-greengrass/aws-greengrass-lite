// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/process.h"
#include <assert.h>
#include <errno.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef SYS_close_range
#include <linux/close_range.h>
#endif

static void sigalrm_handler(int s) {
    (void) s;
}

// Lowest allowed priority in order to run before threads are created.
__attribute__((constructor(101))) static void setup_sigalrm(void) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGALRM);
    int sys_ret = pthread_sigmask(SIG_BLOCK, &set, NULL);
    if (sys_ret != 0) {
        GGL_LOGE("pthread_sigmask failed: %d", sys_ret);
        _Exit(1);
    }

    struct sigaction act = { .sa_handler = sigalrm_handler };
    sigaction(SIGALRM, &act, NULL);
}

#ifdef SYS_close_range
static int sys_close_range(unsigned first, unsigned last, unsigned flags) {
    return (int) syscall(SYS_close_range, first, last, flags);
}
#else
static int sys_close_range(unsigned first, unsigned last, unsigned flags) {
    (void) flags;
    int max_fd = (int) sysconf(_SC_OPEN_MAX);
    int range_last = (last < (unsigned) max_fd) ? (int) last : max_fd;
    for (int i = (int) first; i < range_last; i++) {
        close(i);
    }
    return 0;
}

#define CLOSE_RANGE_UNSHARE 2
#endif

GglError ggl_process_spawn(char *const argv[], int *handle) {
    assert(argv[0] != NULL);
    assert(handle != NULL);

    pid_t pid = fork();

    if (pid == 0) {
        sys_close_range(3, UINT_MAX, CLOSE_RANGE_UNSHARE);

        execvp(argv[0], argv);

        _Exit(1);
    }

    if (pid < 0) {
        GGL_LOGE("Err %d when calling fork.", errno);
        return GGL_ERR_FAILURE;
    }

    *handle = pid;
    return GGL_ERR_OK;
}

GglError ggl_process_wait(int handle, bool *exit_status) {
    while (true) {
        siginfo_t info = { 0 };
        int ret = waitid(P_PID, (id_t) handle, &info, WEXITED);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            GGL_LOGE("Err %d when calling waitid.", errno);
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

GglError ggl_process_kill(int handle, uint32_t term_timeout) {
    if (term_timeout == 0) {
        kill(handle, SIGKILL);
        return ggl_process_wait(handle, NULL);
    }

    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, SIGALRM);

    sigset_t old_set;

    kill(handle, SIGTERM);

    // Prevent multiple threads from unblocking SIGALRM
    static pthread_mutex_t sigalrm_mtx = PTHREAD_MUTEX_INITIALIZER;

    int waitid_ret;
    int waitid_err;

    {
        GGL_MTX_SCOPE_GUARD(&sigalrm_mtx);

        pthread_sigmask(SIG_SETMASK, &set, &old_set);

        alarm(term_timeout);

        siginfo_t info = { 0 };
        waitid_ret = waitid(P_PID, (id_t) handle, &info, WEXITED);
        waitid_err = errno;

        alarm(0);

        pthread_sigmask(SIG_SETMASK, &old_set, NULL);
    }

    if (waitid_ret == 0) {
        return GGL_ERR_OK;
    }

    if (waitid_err != EINTR) {
        GGL_LOGE("Err %d when calling waitid.", errno);
        return GGL_ERR_FAILURE;
    }

    kill(handle, SIGKILL);

    return ggl_process_wait(handle, NULL);
}

GglError ggl_process_call(char *const argv[]) {
    int handle;
    GglError ret = ggl_process_spawn(argv, &handle);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    bool exit_status = false;
    ret = ggl_process_wait(handle, &exit_status);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return exit_status ? GGL_ERR_OK : GGL_ERR_FAILURE;
}
