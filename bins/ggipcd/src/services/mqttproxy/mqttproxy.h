// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IPC_SERVICE_MQTTPROXY_H
#define GGL_IPC_SERVICE_MQTTPROXY_H

#include "../../ipc_authz.h"
#include "../../ipc_service.h"

GglIpcOperationHandler ggl_handle_publish_to_iot_core;
GglIpcOperationHandler ggl_handle_subscribe_to_iot_core;

GglIpcPolicyResourceMatcher ggl_ipc_mqtt_policy_matcher;

#endif
