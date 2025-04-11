// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGIPC_CLIENT_H
#define GGIPC_CLIENT_H

#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <ggl/object.h>
#include <stdint.h>

#define GGL_IPC_SVCUID_LEN (16)

/// Connect to GG-IPC server using component name.
/// If svcuid is non-null, it will be filled with the component's identity
/// token.
GglError ggipc_connect_by_name(
    GglBuffer socket_path, GglBuffer component_name, GglBuffer *svcuid, int *fd
);

GglError ggipc_call(
    int conn,
    GglBuffer operation,
    GglBuffer service_model_type,
    GglMap params,
    GglAlloc *alloc,
    GglObject *result,
    GglIpcError *remote_err
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
