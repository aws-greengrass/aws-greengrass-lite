// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "authorization_agent.h"
#include "../../ipc_service.h"
#include <ggl/buffer.h>

static GglIpcOperation operations[] = {
    {
        GGL_STR("aws.greengrass#ValidateAuthorizationToken"),
        ggl_handle_token_validation,
    },
};

GglIpcService ggl_ipc_service_token_validation = {
    .name = GGL_STR("aws.greengrass.authorizationagent"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
