// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_PROCESS_H
#define GGL_PROCESS_H

//! Process management functionality

#include <gg/error.h>
#include <stdbool.h>
#include <stdint.h>

/// Handle for a process
typedef struct {
    int32_t val;
} GglProcessHandle;

/// Spawn a child process with given arguments
/// Exactly one of wait or kill must eventually be called to clean up resources
/// and reap zombie.
/// argv must be null-terminated.
GgError ggl_process_spawn(const char *const argv[], GglProcessHandle *handle);

/// Run a process with given arguments, and return if successful.
/// argv must be null-terminated.
GgError ggl_process_call(const char *const argv[]);

/// Wait until child process exits
/// Cleans up handle and child zombie.
/// Will return OK if cleanup is successful, regardless of exit status.
GgError ggl_process_wait(GglProcessHandle handle, bool *exit_status);

/// Kill a child process
/// If term_timeout > 0, first sends SIGTERM and waits up to timeout.
/// If term_timeout == 0, or timeout elapses, sends SIGKILL.
/// Cleans up handle and child zombie.
GgError ggl_process_kill(GglProcessHandle handle, uint32_t term_timeout);

#endif
