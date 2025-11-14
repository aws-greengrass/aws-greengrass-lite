// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "cli.h"
#include "../../ipc_service.h"
#include <gg/buffer.h>

static GglIpcOperation operations[] = {
    {
        GG_STR("aws.greengrass#CreateLocalDeployment"),
        ggl_handle_create_local_deployment,
    },
    {
        GG_STR("aws.greengrass#RestartComponent"),
        ggl_handle_restart_component,
    },
};

GglIpcService ggl_ipc_service_cli = {
    .name = GG_STR("aws.greengrass.Cli"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
