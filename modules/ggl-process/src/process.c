// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/process.h>
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
        GG_LOGE("pthread_sigmask failed: %d", sys_ret);
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

#define CLOSE_RANGE_UNSHARE 2

static int sys_close_range(unsigned first, unsigned last, unsigned flags) {
    if (flags & CLOSE_RANGE_UNSHARE) {
        unshare(CLONE_FILES);
    }
    int max_fd = (int) sysconf(_SC_OPEN_MAX) - 1;
    int range_last = (last < (unsigned) max_fd) ? (int) last : max_fd;
    for (int i = (int) first; i <= range_last; i++) {
        close(i);
    }
    return 0;
}

#endif

void ggl_close_fds_from(unsigned int first) {
    sys_close_range(first, UINT_MAX, CLOSE_RANGE_UNSHARE);
}

static GgError child_setup_and_exec(
    const char *const argv[], GglProcessSpawnConfig cfg
) {
    if (cfg.child_setup != NULL) {
        GgError err = cfg.child_setup(cfg.child_setup_ctx);
        if (err != GG_ERR_OK) {
            return err;
        }
    }

    if (!cfg.keep_stdin || cfg.null_stdout || cfg.null_stderr) {
        int dev_null = open("/dev/null", O_RDWR | O_CLOEXEC);
        if (dev_null < 0) {
            GG_LOGE("Failed to open /dev/null: %d", errno);
            return GG_ERR_FAILURE;
        }
        if (!cfg.keep_stdin) {
            dup2(dev_null, STDIN_FILENO);
        }
        if (cfg.null_stdout) {
            dup2(dev_null, STDOUT_FILENO);
        }
        if (cfg.null_stderr) {
            dup2(dev_null, STDERR_FILENO);
        }
        close(dev_null);
    }

    if (!cfg.keep_fds) {
        ggl_close_fds_from(3);
    }

    execvp(argv[0], (char **) argv);

    GG_LOGE("Err %d when calling execve.", errno);
    return GG_ERR_FAILURE;
}

GgError ggl_process_spawn(
    const char *const argv[],
    const GglProcessSpawnConfig *config,
    GglProcessHandle handle[static 1]
) {
    assert(argv[0] != NULL);

    GglProcessSpawnConfig cfg = { 0 };
    if (config != NULL) {
        cfg = *config;
    }

    int err_pipe[2];
    if (pipe2(err_pipe, O_CLOEXEC) < 0) {
        GG_LOGE("Err %d when calling pipe2.", errno);
        return GG_ERR_FATAL;
    }
    GG_CLEANUP(cleanup_close, err_pipe[0])

    pid_t pid = fork();

    if (pid == 0) {
        GgError child_err = child_setup_and_exec(argv, cfg);
        (void) gg_file_write(
            err_pipe[1],
            (GgBuffer) { (uint8_t *) &child_err, sizeof(child_err) }
        );
        _Exit(1);
    }

    (void) gg_close(err_pipe[1]);

    if (pid < 0) {
        GG_LOGE("Err %d when calling fork.", errno);
        return GG_ERR_FATAL;
    }

    GgError child_err;
    GgBuffer err_buf = { (uint8_t *) &child_err, sizeof(child_err) };
    GgError ret = gg_file_read(err_pipe[0], &err_buf);
    if (ret != GG_ERR_OK) {
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return ret;
    }

    if (err_buf.len != 0) {
        assert(err_buf.len == sizeof(child_err));
        // Failed to setup or exec in child; reap it.
        waitpid(pid, NULL, 0);
        return child_err;
    }

    *handle = (GglProcessHandle) { .val = pid };
    return GG_ERR_OK;
}

GgError ggl_process_wait(GglProcessHandle handle, bool *exit_status) {
    while (true) {
        siginfo_t info = { 0 };
        int ret = waitid(P_PID, (id_t) handle.val, &info, WEXITED);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            GG_LOGE("Err %d when calling waitid.", errno);
            return GG_ERR_FATAL;
        }

        switch (info.si_code) {
        case CLD_EXITED:
            if (exit_status != NULL) {
                *exit_status = info.si_status == 0;
            }
            return GG_ERR_OK;
        case CLD_KILLED:
        case CLD_DUMPED:
            if (exit_status != NULL) {
                *exit_status = false;
            }
            return GG_ERR_OK;
        default:;
        }
    }
}

GgError ggl_process_kill(GglProcessHandle handle, uint32_t term_timeout) {
    if (term_timeout == 0) {
        kill(handle.val, SIGKILL);
        return ggl_process_wait(handle, NULL);
    }

    sigset_t set;
    sigfillset(&set);
    sigdelset(&set, SIGALRM);

    sigset_t old_set;

    kill(handle.val, SIGTERM);

    // Prevent multiple threads from unblocking SIGALRM
    static pthread_mutex_t sigalrm_mtx = PTHREAD_MUTEX_INITIALIZER;

    int waitid_ret;
    int waitid_err;

    {
        GG_MTX_SCOPE_GUARD(&sigalrm_mtx);

        pthread_sigmask(SIG_SETMASK, &set, &old_set);

        alarm(term_timeout);

        siginfo_t info = { 0 };
        waitid_ret = waitid(P_PID, (id_t) handle.val, &info, WEXITED);
        waitid_err = errno;

        alarm(0);

        pthread_sigmask(SIG_SETMASK, &old_set, NULL);
    }

    if (waitid_ret == 0) {
        return GG_ERR_OK;
    }

    if (waitid_err != EINTR) {
        GG_LOGE("Err %d when calling waitid.", waitid_err);
        return GG_ERR_FATAL;
    }

    kill(handle.val, SIGKILL);

    return ggl_process_wait(handle, NULL);
}

GgError ggl_process_call(const char *const argv[]) {
    GglProcessHandle handle = { 0 };
    GgError ret = ggl_process_spawn(argv, NULL, &handle);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    bool exit_status = false;
    ret = ggl_process_wait(handle, &exit_status);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    return exit_status ? GG_ERR_OK : GG_ERR_FAILURE;
}
