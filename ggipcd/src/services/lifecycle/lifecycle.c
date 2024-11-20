// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "lifecycle.h"
#include "../../ipc_service.h"
#include <ggl/object.h>

static GglIpcOperation operations[] = { {
    GGL_STR("aws.greengrass#UpdateState"),
    ggl_handle_update_state,
} };

GglIpcService ggl_ipc_service_lifecycle = {
    .name = GGL_STR("aws.greengrass.ipc.lifecycle"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
