// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "shadow.h"
#include "../../ipc_service.h"
#include <gg/buffer.h>

static GglIpcOperation operations[] = {
    {
        GG_STR("aws.greengrass#GetThingShadow"),
        ggl_handle_get_thing_shadow,
    },
    {
        GG_STR("aws.greengrass#UpdateThingShadow"),
        ggl_handle_update_thing_shadow,
    },
    {
        GG_STR("aws.greengrass#DeleteThingShadow"),
        ggl_handle_delete_thing_shadow,
    },
    {
        GG_STR("aws.greengrass#ListNamedShadowsForThing"),
        ggl_handle_list_named_shadows_for_thing,
    },
};

GglIpcService ggl_ipc_service_shadow = {
    .name = GG_STR("aws.greengrass.ipc.shadow"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
