/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EXEC_H
#define GGL_EXEC_H

#include "ggl/io.h"
#include <ggl/error.h>
#include <ggl/object.h>
#include <sys/types.h>

GglError ggl_exec_command(const char *const args[static 1]);
GglError ggl_exec_command_async(
    const char *const args[static 1], pid_t child_pid[static 1]
);
GglError ggl_exec_kill_process(pid_t process_id);

GglError ggl_exec_command_with_output(
    const char *const args[static 1], GglWriter writer
);

GglError ggl_exec_command_with_input(
    const char *const args[static 1], GglObject payload
);

#endif
