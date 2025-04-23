/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/exec.h"
#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

GglError ggl_exec_command(char *args[]) {
    int pid = -1;
    GglError err = ggl_exec_command_async(args, &pid);
    if (err != GGL_ERR_OK) {
        return err;
    }

    int child_status;
    if (waitpid(pid, &child_status, 0) == -1) {
        GGL_LOGE("Error, waitpid got hit");
        return GGL_ERR_FAILURE;
    }
    if (!WIFEXITED(child_status)) {
        GGL_LOGD("Script did not exit normally");
        return GGL_ERR_FAILURE;
    }
    GGL_LOGI("Script exited with child status %d\n", WEXITSTATUS(child_status));
    if (WEXITSTATUS(child_status) != 0) {
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError ggl_exec_command_async(char *args[], pid_t *child_pid) {
    pid_t pid;
    int ret = posix_spawnp(&pid, args[0], NULL, NULL, args, environ);
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
