/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EXEC_H
#define GGL_EXEC_H

#include <sys/types.h>
#include <ggl/error.h>

GglError ggl_exec_command(char *args[]);
GglError ggl_exec_command_async(char *args[], pid_t *child_pid);
GglError ggl_exec_kill_process(pid_t process_id);

#endif
