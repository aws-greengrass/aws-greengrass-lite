// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGIPC_CLIENT_H
#define GGIPC_CLIENT_H

#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdint.h>

#ifndef GGL_IPC_AUTH_DISABLE
#define GGL_IPC_MAX_SVCUID_LEN (16)
#else
// Max component name length
#define GGL_IPC_MAX_SVCUID_LEN (128)
#endif

/// Connect to GG-IPC server, requesting an authentication token
GglError ggipc_connect_auth(GglBuffer socket_path, GglBuffer *svcuid, int *fd);

GglError ggipc_call(
    int conn,
    GglBuffer operation,
    GglMap params,
    GglAlloc *alloc,
    GglObject *result
) __attribute__((warn_unused_result));

GglError ggipc_private_get_system_config(
    int conn, GglBuffer key, GglBuffer *value
);

GglError ggipc_get_config_str(
    int conn, GglBufList key_path, GglBuffer *component_name, GglBuffer *value
);

GglError ggipc_get_config_obj(
    int conn,
    GglBufList key_path,
    GglBuffer *component_name,
    GglAlloc *alloc,
    GglObject *value
);

GglError ggipc_publish_to_iot_core(
    int conn,
    GglBuffer topic_name,
    GglBuffer payload,
    uint8_t qos,
    GglAlloc *alloc
);

#endif
