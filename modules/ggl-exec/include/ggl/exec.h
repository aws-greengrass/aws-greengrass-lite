/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EXEC_H
#define GGL_EXEC_H

#include <gg/error.h>
#include <gg/io.h>
#include <gg/types.h>
#include <sys/types.h>

GgError ggl_exec_command(const char *const args[static 1]);
GgError ggl_exec_command_async(
    const char *const args[static 1], pid_t child_pid[static 1]
);
GgError ggl_exec_kill_process(pid_t process_id);

GgError ggl_exec_command_with_output(
    const char *const args[static 1], GgWriter writer
);

GgError ggl_exec_command_with_input(
    const char *const args[static 1], GgObject payload
);

#endif
