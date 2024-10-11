// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "mqttproxy.h"
#include "../../ipc_authz.h"
#include "../../ipc_service.h"
#include <ggl/buffer.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>

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

/// Matches topic or topic filter against topic filter
/// i.e. `a/+/b` matches `a/#`
static bool match_topic_filter(GglBuffer resource, GglBuffer filter) {
    size_t i = 0;
    size_t j = 0;
    while (i < filter.len) {
        switch (filter.data[i]) {
        case '+':
            while ((j < resource.len) && (resource.data[j] != '/')) {
                j++;
            }
            break;
        case '#':
            return true;
        default:
            if (filter.data[i] != resource.data[j]) {
                return false;
            }
            j++;
        }
        i++;
    }
    return j == resource.len;
}

bool ggl_ipc_mqtt_policy_matcher(
    GglBuffer request_resource, GglBuffer policy_resource
) {
    return ggl_ipc_default_policy_matcher(request_resource, policy_resource)
        || match_topic_filter(request_resource, policy_resource);
}
