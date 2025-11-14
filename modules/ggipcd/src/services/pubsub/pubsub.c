// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "pubsub.h"
#include "../../ipc_service.h"
#include <gg/buffer.h>

static GglIpcOperation operations[] = {
    {
        GG_STR("aws.greengrass#PublishToTopic"),
        ggl_handle_publish_to_topic,
    },
    {
        GG_STR("aws.greengrass#SubscribeToTopic"),
        ggl_handle_subscribe_to_topic,
    },
};

GglIpcService ggl_ipc_service_pubsub = {
    .name = GG_STR("aws.greengrass.ipc.pubsub"),
    .operations = operations,
    .operation_count = sizeof(operations) / sizeof(*operations),
};
