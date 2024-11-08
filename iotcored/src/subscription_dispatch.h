// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_SUBSCRIPTION_DISPATCH_H
#define IOTCORED_SUBSCRIPTION_DISPATCH_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

GglError iotcored_register_subscriptions(
    GglBuffer *topic_filters, size_t count, uint32_t handle, uint8_t qos
);

void iotcored_unregister_subscriptions(uint32_t handle, bool unsub);

void iotcored_re_register_all_subs(void);

GglError iotcored_mqtt_status_update_register(uint32_t handle);

void iotcored_mqtt_status_update_unregister(uint32_t handle);

void iotcored_mqtt_status_update_send(GglObject status);

#endif
