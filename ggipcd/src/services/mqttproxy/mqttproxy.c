// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mqttproxy.h"
#include "../../ipc_service.h"
#include <ggl/object.h>

static GglIpcOperation operations[] = {
    {
        GGL_STR("aws.greengrass#PublishToIoTCore"),
        ggl_handle_publish_to_iot_core,
    },
    {
        GGL_STR("aws.greengrass#SubscribeToIoTCore"),
        ggl_handle_subscribe_to_iot_core,
    },
};

GglIpcService ggl_ipc_service_mqttproxy = {
    .name = GGL_STR("aws.greengrass.ipc.mqttproxy"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
