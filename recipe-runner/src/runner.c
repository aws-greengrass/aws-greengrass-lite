// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "runner.h"
#include "ggipc/client.h"
#include "recipe-runner.h"
#include <sys/types.h>
#include <errno.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_SCRIPT_LENGTH 10000

pid_t child_pid = -1; // To store child process ID

GglError get_file_content(const char *file_path, char *return_value) {
    FILE *file = fopen(file_path, "r");
    if (file == NULL) {
        int err = errno;
        GGL_LOGE("recipe-runner", "Error opening file: %d", err);
        return GGL_ERR_INVALID;
    }
    // Get the file size
    fseek(file, 0, SEEK_END);
    size_t file_size = (size_t) ftell(file);
    fseek(file, 0, SEEK_SET);

    // Read the file into the content buffer
    ulong value_read = fread(return_value, 1, file_size, file);
    return_value[file_size] = '\0'; // Null-terminate the string

    if (value_read != file_size) {
        GGL_LOGE("recipe-runner", "Failed to read the complete file");
        return GGL_ERR_PARSE;
    }

    fclose(file);
    return GGL_ERR_OK;
}

GglError runner(const RecipeRunnerArgs *args) {
    static char script[MAX_SCRIPT_LENGTH] = { 0 };

    // Get the SocketPath from Environment Variable
    // NOLINTBEGIN(concurrency-mt-unsafe)

    char *socket_path
        = getenv("AWS_GG_NUCLEUS_DOMAIN_SOCKET_FILEPATH_FOR_COMPONENT");

    if (socket_path == NULL) {
        GGL_LOGE("recipe-runner", "SocketPath environment not set....");
        return GGL_ERR_FAILURE;
    }
    // NOLINTEND(concurrency-mt-unsafe)

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

    // Fetch the bash script content to memory
    ret = get_file_content(args->file_path, script);
    if (ret != GGL_ERR_OK) {
        return ret;
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

        // If execvpe returns, it must have failed
        GGL_LOGE("recipe-runner", "Error: execvpe returned unexpectedly");
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
