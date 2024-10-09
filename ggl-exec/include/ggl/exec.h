/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_EXEC__H
#define GGL_EXEC__H

#include <sys/types.h>
#include <ggl/error.h>

GglError exec_command_with_child_wait(char *args[], pid_t *out_child_pid);
GglError exec_command_without_child_wait(char *args[], pid_t *out_child_pid);
GglError exec_kill_process(pid_t process_id);

#endif
