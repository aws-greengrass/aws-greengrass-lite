/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_IPC_SERVER_H
#define GGL_IPC_SERVER_H

#include <ggl/error.h>

GglError ggl_ipc_listen(const char *socket_path);

#endif
