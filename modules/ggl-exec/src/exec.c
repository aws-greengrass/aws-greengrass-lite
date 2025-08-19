/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/exec.h"
#include "ggl/json_encode.h"
#include "priv_io.h"
#include <errno.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/io.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
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

GglError ggl_exec_command(const char *const args[static 1]) {
    int pid = -1;
    GglError err = ggl_exec_command_async(args, &pid);
    if (err != GGL_ERR_OK) {
        return err;
    }

    return wait_for_process(pid);
}

GglError ggl_exec_command_async(
    const char *const args[static 1], pid_t child_pid[static 1]
) {
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

static void cleanup_posix_destroy_file_actions(
    posix_spawn_file_actions_t **actions
) {
    if ((actions != NULL) && (*actions != NULL)) {
        (void) posix_spawn_file_actions_destroy(*actions);
    }
}

// configures a pipe to redirect stdout,stderr
static GglError create_output_pipe_file_actions(
    posix_spawn_file_actions_t actions[static 1],
    int pipe_read_fd,
    int pipe_write_fd
) {
    // The child does not need the readable end.
    int ret = posix_spawn_file_actions_addclose(actions, pipe_read_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    // Redirect both stderr and stdout to the writeable end
    ret = posix_spawn_file_actions_adddup2(
        actions, pipe_write_fd, STDOUT_FILENO
    );
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    ret = posix_spawn_file_actions_adddup2(
        actions, pipe_write_fd, STDERR_FILENO
    );
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    ret = posix_spawn_file_actions_addclose(actions, pipe_write_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

// configures a pipe to stdin
static GglError create_input_pipe_file_actions(
    posix_spawn_file_actions_t actions[static 1],
    int pipe_read_fd,
    int pipe_write_fd
) {
    // The child does not need the writeable end.
    int ret = posix_spawn_file_actions_addclose(actions, pipe_write_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    // Redirect stdin to the readable pipe
    ret = posix_spawn_file_actions_adddup2(actions, pipe_read_fd, STDIN_FILENO);
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    ret = posix_spawn_file_actions_addclose(actions, pipe_read_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GGL_ERR_NOMEM : GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

// Read from pipe until EOF is found.
// Writer is called until its first error is returned.
// Pipe is flushed to allow child to exit cleanly.
static GglError pipe_flush(int pipe_read_fd, GglWriter writer) {
    GglError writer_error = GGL_ERR_OK;
    while (true) {
        uint8_t partial_buf[256];
        GglBuffer partial = GGL_BUF(partial_buf);
        GglError read_err = ggl_file_read(pipe_read_fd, &partial);
        if (read_err == GGL_ERR_RETRY) {
            continue;
        }
        if (read_err != GGL_ERR_OK) {
            return read_err;
        }
        if (writer_error == GGL_ERR_OK) {
            writer_error = ggl_writer_call(writer, partial);
        }
        // EOF (pipe closed)
        if (partial.len < sizeof(partial_buf)) {
            return writer_error;
        }
    }
}

GglError ggl_exec_command_with_output(
    const char *const args[static 1], GglWriter writer
) {
    int out_pipe[2] = { -1, -1 };
    int ret = pipe(out_pipe);
    if (ret != 0) {
        GGL_LOGE("Failed to create pipe (errno=%d).", errno);
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP(cleanup_close, out_pipe[0]);
    GGL_CLEANUP_ID(pipe_write_cleanup, cleanup_close, out_pipe[1]);

    posix_spawn_file_actions_t actions = { 0 };
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return GGL_ERR_NOMEM;
    }
    GGL_CLEANUP_ID(
        actions_cleanup, cleanup_posix_destroy_file_actions, &actions
    );
    GglError err
        = create_output_pipe_file_actions(&actions, out_pipe[0], out_pipe[1]);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to create posix spawn file actions.");
        return GGL_ERR_FAILURE;
    }

    pid_t pid = -1;
    ret = posix_spawnp(
        &pid, args[0], &actions, NULL, (char *const *) args, environ
    );
    if (ret != 0) {
        GGL_LOGE("Error, unable to spawn (%d)", ret);
        return GGL_ERR_FAILURE;
    }

    (void) posix_spawn_file_actions_destroy(&actions);
    actions_cleanup = NULL;
    (void) ggl_close(pipe_write_cleanup);
    pipe_write_cleanup = -1;

    GglError read_err = pipe_flush(out_pipe[0], writer);
    GglError process_err = wait_for_process(pid);

    if (process_err != GGL_ERR_OK) {
        return process_err;
    }
    return read_err;
}

GglError ggl_exec_command_with_input(
    const char *const args[static 1], GglObject payload
) {
    int in_pipe[2] = { -1, -1 };
    int ret = pipe(in_pipe);
    if (ret < 0) {
        return GGL_ERR_FAILURE;
    }
    GGL_CLEANUP_ID(pipe_read_cleanup, cleanup_close, in_pipe[0]);
    GGL_CLEANUP_ID(pipe_write_cleanup, cleanup_close, in_pipe[1]);

    posix_spawn_file_actions_t actions = { 0 };
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return GGL_ERR_NOMEM;
    }
    GGL_CLEANUP_ID(
        actions_cleanup, cleanup_posix_destroy_file_actions, &actions
    );
    GglError err
        = create_input_pipe_file_actions(&actions, in_pipe[0], in_pipe[1]);
    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to create posix spawn file actions.");
        return GGL_ERR_FAILURE;
    }

    pid_t pid = -1;
    ret = posix_spawnp(
        &pid, args[0], &actions, NULL, (char *const *) args, environ
    );
    if (ret != 0) {
        GGL_LOGE("Error, unable to spawn (%d)", ret);
        return GGL_ERR_FAILURE;
    }

    (void) posix_spawn_file_actions_destroy(&actions);
    actions_cleanup = NULL;
    (void) ggl_close(pipe_read_cleanup);
    pipe_read_cleanup = -1;

    GglError pipe_error = GGL_ERR_OK;
    if (ggl_obj_type(payload) == GGL_TYPE_BUF) {
        pipe_error = ggl_file_write(in_pipe[1], ggl_obj_into_buf(payload));
    } else {
        FileWriterContext ctx = { .fd = in_pipe[1] };
        pipe_error = ggl_json_encode(payload, priv_file_writer(&ctx));
    }
    (void) ggl_close(pipe_write_cleanup);
    pipe_write_cleanup = -1;

    GglError process_err = wait_for_process(pid);

    if (process_err != GGL_ERR_OK) {
        return err;
    }
    return pipe_error;
}
