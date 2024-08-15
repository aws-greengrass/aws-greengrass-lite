// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGIPC_CLIENT_H
#define GGIPC_CLIENT_H

#include <ggl/error.h>
#include <ggl/object.h>

/// Connect to GG-IPC server, requesting an authentication token
GglError ggipc_connect_auth(GglBuffer socket_path, GglBuffer *svcuid, int *fd);

#endif
