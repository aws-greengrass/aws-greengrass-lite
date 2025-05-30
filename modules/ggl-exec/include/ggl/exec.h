/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EXEC_H
#define GGL_EXEC_H

#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <sys/types.h>

GglError ggl_exec_command(const char *const args[]) NONNULL(1);
GglError ggl_exec_command_async(const char *const args[], pid_t *child_pid)
    NONNULL(1, 2);
GglError ggl_exec_kill_process(pid_t process_id);
GglError ggl_exec_command_with_output(
    const char *const args[], GglBuffer *output
) NONNULL(1, 2);

#endif
