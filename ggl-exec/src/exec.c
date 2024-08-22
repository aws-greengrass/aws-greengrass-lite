/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/exec.h"
#include <sys/types.h>
#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

GglError exec_command(char *args[], pid_t *child_pid) {
    GglError return_status = GGL_ERR_OK;

    // Fork so that parent can live after execvp
    pid_t pid = fork();

    if (pid == -1) { // Something went wrong
        GGL_LOGE("exec-lib", "Error, Unable to fork");
        return_status = GGL_ERR_FAILURE;

    } else if (pid == 0) { // Child process: execute the script
        // char *exec_args[] = { "bash", args->file_path, NULL };
        execvp(args[0], args);

        // If execvpe returns, it must have failed
        GGL_LOGE("exec-lib", "Error: execvpe returned unexpectedly");
        return_status = GGL_ERR_FAILURE;

    } else { // Parent process: wait for the child to finish

        *child_pid = pid; // Store the child process ID

        int child_status;
        if (waitpid(pid, &child_status, 0) == -1) {
            GGL_LOGE("exec-lib", "Error, waitpid got hit");
        } else {
            if (WIFEXITED(child_status)) {
                if (WEXITSTATUS(child_status) != 0) {
                    return_status = GGL_ERR_FAILURE;
                }
                GGL_LOGI(
                    "exec-lib",
                    "Script exited with child status %d\n",
                    WEXITSTATUS(child_status)
                );

            } else {
                GGL_LOGD("exec-lib", "Script did not exit normally");
                return_status = GGL_ERR_FAILURE;
            }
        }
    }
    return return_status;
}

GglError exec_kill_process(pid_t process_id) {
    // Send the SIGTERM signal to the process

    // NOLINTBEGIN(concurrency-mt-unsafe)
    if (kill(process_id, SIGTERM) == -1) {
        GGL_LOGI(
            "exec-lib",
            "Failed to kill the process id %d : %s errno:%d.",
            process_id,
            strerror(errno),
            errno
        );
        return GGL_ERR_FAILURE;
    }
    // NOLINTEND(concurrency-mt-unsafe)

    return GGL_ERR_OK;
}
