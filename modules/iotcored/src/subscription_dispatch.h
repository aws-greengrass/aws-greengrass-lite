// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_SUBSCRIPTION_DISPATCH_H
#define IOTCORED_SUBSCRIPTION_DISPATCH_H

#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// Register topic filters for on-device routing under `handle`.
/// `is_virtual` marks the registration as having no cloud subscription:
/// matching inbound messages are still routed to `handle`, but the filter is
/// never subscribed, unsubscribed, or re-subscribed with AWS IoT Core.
GgError iotcored_register_subscriptions(
    GgBuffer *topic_filters,
    size_t count,
    uint32_t handle,
    uint8_t qos,
    bool is_virtual
);

void iotcored_unregister_subscriptions(uint32_t handle, bool unsubscribe);

void iotcored_re_register_all_subs(void);

GgError iotcored_mqtt_status_update_register(uint32_t handle);

void iotcored_mqtt_status_update_unregister(uint32_t handle);

void iotcored_mqtt_status_update_send(GgObject status);

#endif
