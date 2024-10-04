// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runner.h"
#include "ggipc/client.h"
#include "recipe-runner.h"
#include <sys/types.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_SCRIPT_LENGTH 10000

pid_t child_pid = -1; // To store child process ID

GglError runner(const RecipeRunnerArgs *args) {
    // Get the SocketPath from Environment Variable
    // NOLINTBEGIN(concurrency-mt-unsafe)
    char *socket_path
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");
    // NOLINTEND(concurrency-mt-unsafe)

    if (socket_path == NULL) {
        GGL_LOGE("recipe-runner", "IPC socket path env var not set.");
        return GGL_ERR_FAILURE;
    }

    // Fetch the SVCUID

    // Includes null-termination
    static char svcuid_env_buf[GGL_IPC_MAX_SVCUID_LEN + sizeof("SVCUID=")]
        = "SVCUID=";

    GglBuffer svcuid
        = { .data = (uint8_t *) &svcuid_env_buf[sizeof("SVCUID=") - 1],
            .len = GGL_IPC_MAX_SVCUID_LEN };
    GglError ret = ggipc_connect_auth(
        ((GglBuffer) { .data = (uint8_t *) socket_path,
                       .len = strlen(socket_path) }),
        &svcuid,
        NULL
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    svcuid.data[svcuid.len] = '\0';

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int set_env_status = putenv(svcuid_env_buf);

    if (set_env_status == -1) {
        GGL_LOGE("recipe-runner", "Failed to set SVCUID environment variable");
    }

    // Fork so that parent can live after execvp
    pid_t pid = fork();
    GglError return_status = GGL_ERR_OK;

    if (pid == -1) {
        // Something went wrong

        GGL_LOGE("recipe-runner", "Error, Unable to fork");
        return_status = GGL_ERR_FATAL;
    } else if (pid == 0) {
        // Child process: execute the script

        char *exec_args[] = { "bash", args->file_path, NULL };
        execvp("bash", exec_args);

        // If execvp returns, it must have failed
        GGL_LOGE("recipe-runner", "Error: execvp returned unexpectedly");
        return_status = GGL_ERR_FATAL;
    } else {
        // Parent process: wait for the child to finish

        child_pid = pid; // Store the child process ID

        int child_status;
        if (waitpid(pid, &child_status, 0) == -1) {
            GGL_LOGE("recipe-runner", "Error, waitpid got hit");
            return_status = GGL_ERR_FATAL;
        } else {
            if (WIFEXITED(child_status)) {
                GGL_LOGI(
                    "recipe-runner",
                    "Script exited with child status %d\n",
                    WEXITSTATUS(child_status)
                );
            } else {
                GGL_LOGD("recipe-runner", "Script did not exit normally");
            }
        }
    }

    GGL_LOGD("recipe-runner", "Parent return status %d\n", return_status);
    return return_status;
}
