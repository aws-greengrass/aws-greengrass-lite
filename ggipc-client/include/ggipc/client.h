// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGIPC_CLIENT_H
#define GGIPC_CLIENT_H

#include <ggl/error.h>
#include <ggl/object.h>

#ifndef GGL_IPC_AUTH_DISABLE
#define GGL_IPC_MAX_SVCUID_LEN (16)
#else
// Max component name length
#define GGL_IPC_MAX_SVCUID_LEN (128)
#endif

/// Connect to GG-IPC server, requesting an authentication token
GglError ggipc_connect_auth(GglBuffer socket_path, GglBuffer *svcuid, int *fd);

#endif
