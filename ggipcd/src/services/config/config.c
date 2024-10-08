// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "config.h"
#include "../../ipc_service.h"
#include <ggl/object.h>

static GglIpcOperation operations[] = {
    {
        GGL_STR("aws.greengrass#GetConfiguration"),
        ggl_handle_get_configuration,
    },
    {
        GGL_STR("aws.greengrass#UpdateConfiguration"),
        ggl_handle_update_configuration,
    },
    {
        GGL_STR("aws.greengrass#SubscribeToConfigurationUpdate"),
        ggl_handle_subscribe_to_configuration_update,
    },
};

GglIpcService ggl_ipc_service_config = {
    .name = GGL_STR("aws.greengrass.ipc.config"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
