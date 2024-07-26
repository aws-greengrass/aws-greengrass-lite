/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IOTCORED_SUBSCRIPTION_DISPATCH_H
#define IOTCORED_SUBSCRIPTION_DISPATCH_H

#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError iotcored_register_subscription(
    GglBuffer topic_filter, uint32_t handle
);

void iotcored_unregister_subscriptions(uint32_t handle);

#endif
