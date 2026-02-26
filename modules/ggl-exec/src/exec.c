/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "priv_io.h"
#include <errno.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/io.h>
#include <gg/json_encode.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/exec.h>
#include <signal.h>
#include <spawn.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GgError wait_for_process(pid_t pid) {
    int child_status;
    if (waitpid(pid, &child_status, 0) == -1) {
        GG_LOGE("Error, waitpid got hit");
        return GG_ERR_FAILURE;
    }
    if (!WIFEXITED(child_status)) {
        GG_LOGD("Script did not exit normally");
        return GG_ERR_FAILURE;
    }
    if (WEXITSTATUS(child_status) != 0) {
        GG_LOGE(
            "Script exited with child status %d", WEXITSTATUS(child_status)
        );
        return GG_ERR_FAILURE;
    }

    GG_LOGD("Script exited with child status as success");
    return GG_ERR_OK;
}

GgError ggl_exec_command(const char *const args[static 1]) {
    int pid = -1;
    GgError err = ggl_exec_command_async(args, &pid);
    if (err != GG_ERR_OK) {
        return err;
    }

    return wait_for_process(pid);
}

GgError ggl_exec_command_async(
    const char *const args[static 1], pid_t child_pid[static 1]
) {
    pid_t pid = -1;
    int ret = posix_spawnp(
        &pid, args[0], NULL, NULL, (char *const *) args, environ
    );
    if (ret != 0) {
        GG_LOGE("Error, unable to spawn (%d)", ret);
        return GG_ERR_FAILURE;
    }
    *child_pid = pid;
    return GG_ERR_OK;
}

GgError ggl_exec_kill_process(pid_t process_id) {
    // Send the SIGTERM signal to the process

    // NOLINTBEGIN(concurrency-mt-unsafe, readability-else-after-return)
    if (kill(process_id, SIGTERM) == -1) {
        GG_LOGE(
            "Failed to kill the process id %d : %s errno:%d.",
            process_id,
            strerror(errno),
            errno
        );
        return GG_ERR_FAILURE;
    }

    int status;
    pid_t wait_pid;

    // Wait for the process to terminate
    do {
        wait_pid = waitpid(process_id, &status, 0);
        if (wait_pid == -1) {
            if (errno == ECHILD) {
                GG_LOGE("Process %d has already terminated.\n", process_id);
                break;
            } else {
                GG_LOGE(
                    "Error waiting for process %d: %s (errno: %d)\n",
                    process_id,
                    strerror(errno),
                    errno
                );
                break;
            }
        }

        if (WIFEXITED(status)) {
            GG_LOGE(
                "Process %d exited with status %d.\n",
                process_id,
                WEXITSTATUS(status)
            );
        } else if (WIFSIGNALED(status)) {
            GG_LOGD(
                "Process %d was terminated by signal %d.\n",
                process_id,
                WTERMSIG(status)
            );
        }
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));

    GG_LOGD("Process %d has terminated.\n", process_id);

    // NOLINTEND(concurrency-mt-unsafe, readability-else-after-return)
    return GG_ERR_OK;
}

static void cleanup_posix_destroy_file_actions(
    posix_spawn_file_actions_t **actions
) {
    if ((actions != NULL) && (*actions != NULL)) {
        (void) posix_spawn_file_actions_destroy(*actions);
    }
}

// configures a pipe to redirect stdout,stderr
static GgError create_output_pipe_file_actions(
    posix_spawn_file_actions_t actions[static 1],
    int pipe_read_fd,
    int pipe_write_fd
) {
    // The child does not need the readable end.
    int ret = posix_spawn_file_actions_addclose(actions, pipe_read_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    // Redirect both stderr and stdout to the writeable end
    ret = posix_spawn_file_actions_adddup2(
        actions, pipe_write_fd, STDOUT_FILENO
    );
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    ret = posix_spawn_file_actions_adddup2(
        actions, pipe_write_fd, STDERR_FILENO
    );
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    ret = posix_spawn_file_actions_addclose(actions, pipe_write_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

// configures a pipe to stdin
static GgError create_input_pipe_file_actions(
    posix_spawn_file_actions_t actions[static 1],
    int pipe_read_fd,
    int pipe_write_fd
) {
    // The child does not need the writeable end.
    int ret = posix_spawn_file_actions_addclose(actions, pipe_write_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    // Redirect stdin to the readable pipe
    ret = posix_spawn_file_actions_adddup2(actions, pipe_read_fd, STDIN_FILENO);
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    ret = posix_spawn_file_actions_addclose(actions, pipe_read_fd);
    if (ret != 0) {
        return (ret == ENOMEM) ? GG_ERR_NOMEM : GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

// Read from pipe until EOF is found.
// Writer is called until its first error is returned.
// Pipe is flushed to allow child to exit cleanly.
static GgError pipe_flush(int pipe_read_fd, GgWriter writer) {
    GgError writer_error = GG_ERR_OK;
    while (true) {
        uint8_t partial_buf[256];
        GgBuffer partial = GG_BUF(partial_buf);
        GgError read_err = gg_file_read(pipe_read_fd, &partial);
        if (read_err == GG_ERR_RETRY) {
            continue;
        }
        if (read_err != GG_ERR_OK) {
            return read_err;
        }
        if (writer_error == GG_ERR_OK) {
            writer_error = gg_writer_call(writer, partial);
        }
        // EOF (pipe closed)
        if (partial.len < sizeof(partial_buf)) {
            return writer_error;
        }
    }
}

GgError ggl_exec_command_with_output(
    const char *const args[static 1], GgWriter writer
) {
    int out_pipe[2] = { -1, -1 };
    int ret = pipe(out_pipe);
    if (ret != 0) {
        GG_LOGE("Failed to create pipe (errno=%d).", errno);
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_close, out_pipe[0]);
    GG_CLEANUP_ID(pipe_write_cleanup, cleanup_close, out_pipe[1]);

    posix_spawn_file_actions_t actions = { 0 };
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return GG_ERR_NOMEM;
    }
    GG_CLEANUP_ID(
        actions_cleanup, cleanup_posix_destroy_file_actions, &actions
    );
    GgError err
        = create_output_pipe_file_actions(&actions, out_pipe[0], out_pipe[1]);
    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to create posix spawn file actions.");
        return GG_ERR_FAILURE;
    }

    pid_t pid = -1;
    ret = posix_spawnp(
        &pid, args[0], &actions, NULL, (char *const *) args, environ
    );
    if (ret != 0) {
        GG_LOGE("Error, unable to spawn (%d)", ret);
        return GG_ERR_FAILURE;
    }

    (void) posix_spawn_file_actions_destroy(&actions);
    actions_cleanup = NULL;
    (void) gg_close(pipe_write_cleanup);
    pipe_write_cleanup = -1;

    GgError read_err = pipe_flush(out_pipe[0], writer);
    GgError process_err = wait_for_process(pid);

    if (process_err != GG_ERR_OK) {
        return process_err;
    }
    return read_err;
}

GgError ggl_exec_command_with_input(
    const char *const args[static 1], GgObject payload
) {
    int in_pipe[2] = { -1, -1 };
    int ret = pipe(in_pipe);
    if (ret < 0) {
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP_ID(pipe_read_cleanup, cleanup_close, in_pipe[0]);
    GG_CLEANUP_ID(pipe_write_cleanup, cleanup_close, in_pipe[1]);

    posix_spawn_file_actions_t actions = { 0 };
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return GG_ERR_NOMEM;
    }
    GG_CLEANUP_ID(
        actions_cleanup, cleanup_posix_destroy_file_actions, &actions
    );
    GgError err
        = create_input_pipe_file_actions(&actions, in_pipe[0], in_pipe[1]);
    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to create posix spawn file actions.");
        return GG_ERR_FAILURE;
    }

    pid_t pid = -1;
    ret = posix_spawnp(
        &pid, args[0], &actions, NULL, (char *const *) args, environ
    );
    if (ret != 0) {
        GG_LOGE("Error, unable to spawn (%d)", ret);
        return GG_ERR_FAILURE;
    }

    (void) posix_spawn_file_actions_destroy(&actions);
    actions_cleanup = NULL;
    (void) gg_close(pipe_read_cleanup);
    pipe_read_cleanup = -1;

    GgError pipe_error = GG_ERR_OK;
    if (gg_obj_type(payload) == GG_TYPE_BUF) {
        pipe_error = gg_file_write(in_pipe[1], gg_obj_into_buf(payload));
    } else {
        FileWriterContext ctx = { .fd = in_pipe[1] };
        pipe_error = gg_json_encode(payload, priv_file_writer(&ctx));
    }
    (void) gg_close(pipe_write_cleanup);
    pipe_write_cleanup = -1;

    GgError process_err = wait_for_process(pid);

    if (process_err != GG_ERR_OK) {
        return err;
    }
    return pipe_error;
}
