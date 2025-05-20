/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EXEC_H
#define GGL_EXEC_H

#include <ggl/error.h>
#include <sys/types.h>

GglError ggl_exec_command(const char *const args[]);
GglError ggl_exec_command_async(const char *const args[], pid_t *child_pid);
GglError ggl_exec_kill_process(pid_t process_id);

#endif
