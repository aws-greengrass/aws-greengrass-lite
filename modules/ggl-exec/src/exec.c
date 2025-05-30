/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/exec.h"
#include <errno.h>
#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdlib.h>

static GglError wait_for_process(pid_t pid) {
    int child_status;
    if (waitpid(pid, &child_status, 0) == -1) {
        GGL_LOGE("Error, waitpid got hit");
        return GGL_ERR_FAILURE;
    }
    if (!WIFEXITED(child_status)) {
        GGL_LOGD("Script did not exit normally");
        return GGL_ERR_FAILURE;
    }
    GGL_LOGI("Script exited with child status %d", WEXITSTATUS(child_status));
    if (WEXITSTATUS(child_status) != 0) {
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError ggl_exec_command(const char *const args[]) {
    int pid = -1;
    GglError err = ggl_exec_command_async(args, &pid);
    if (err != GGL_ERR_OK) {
        return err;
    }

    return wait_for_process(pid);
}

GglError ggl_exec_command_async(const char *const args[], pid_t *child_pid) {
    pid_t pid = -1;
    int ret = posix_spawnp(
        &pid, args[0], NULL, NULL, (char *const *) args, environ
    );
    if (ret != 0) {
        GGL_LOGE("Error, unable to spawn (%d)", ret);
        return GGL_ERR_FAILURE;
    }
    *child_pid = pid;
    return GGL_ERR_OK;
}

GglError ggl_exec_kill_process(pid_t process_id) {
    // Send the SIGTERM signal to the process

    // NOLINTBEGIN(concurrency-mt-unsafe, readability-else-after-return)
    if (kill(process_id, SIGTERM) == -1) {
        GGL_LOGE(
            "Failed to kill the process id %d : %s errno:%d.",
            process_id,
            strerror(errno),
            errno
        );
        return GGL_ERR_FAILURE;
    }

    int status;
    pid_t wait_pid;

    // Wait for the process to terminate
    do {
        wait_pid = waitpid(process_id, &status, 0);
        if (wait_pid == -1) {
            if (errno == ECHILD) {
                GGL_LOGE("Process %d has already terminated.\n", process_id);
                break;
            } else {
                GGL_LOGE(
                    "Error waiting for process %d: %s (errno: %d)\n",
                    process_id,
                    strerror(errno),
                    errno
                );
                break;
            }
        }

        if (WIFEXITED(status)) {
            GGL_LOGE(
                "Process %d exited with status %d.\n",
                process_id,
                WEXITSTATUS(status)
            );
        } else if (WIFSIGNALED(status)) {
            GGL_LOGE(
                "Process %d was killed by signal %d.\n",
                process_id,
                WTERMSIG(status)
            );
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    GGL_LOGI("Process %d has terminated.\n", process_id);

    // NOLINTEND(concurrency-mt-unsafe, readability-else-after-return)
    return GGL_ERR_OK;
}

GglError ggl_exec_command_with_output(
    const char *const args[], GglBuffer *output
) {
    char template_cout[] = "/tmp/ggl_exec_stdout_XXXXXX";
    int outfd = mkstemp(template_cout);
    if (outfd == -1) {
        GGL_LOGE("Failed to create output file.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, outfd);
    unlink(template_cout);

    GGL_LOGT("Temporary file created at %s", template_cout);

    char template_cerr[] = "/tmp/ggl_exec_stderr_XXXXXX";
    int errfd = mkstemp(template_cerr);
    if (errfd == -1) {
        GGL_LOGE("Failed to create error file.");
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, errfd);
    unlink(template_cerr);
    GGL_LOGT("Temporary file created at %s", template_cerr);

    posix_spawn_file_actions_t actions = { 0 };
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, outfd, 1);
    posix_spawn_file_actions_addclose(&actions, outfd);
    posix_spawn_file_actions_adddup2(&actions, errfd, 2);
    posix_spawn_file_actions_addclose(&actions, errfd);

    pid_t pid = -1;
    int ret = posix_spawnp(
        &pid, args[0], &actions, NULL, (char *const *) args, environ
    );
    posix_spawn_file_actions_destroy(&actions);

    if (ret != 0) {
        GGL_LOGE("Error, unable to spawn (%d)", ret);
        return GGL_ERR_FAILURE;
    }

    GglError process_ret = wait_for_process(pid);

    GglBuffer remaining = *output;

    if (remaining.len != 0) {
        lseek(errfd, 0, SEEK_SET);
        while (ggl_file_read_partial(errfd, &remaining) == GGL_ERR_RETRY) { }
    }
    GGL_LOGT("%zu", remaining.len);

    if (remaining.len != 0) {
        lseek(outfd, 0, SEEK_SET);
        while (ggl_file_read_partial(outfd, &remaining) == GGL_ERR_RETRY) { }
    }
    GGL_LOGT("%zu", remaining.len);

    output->len -= remaining.len;
    return process_ret;
}
