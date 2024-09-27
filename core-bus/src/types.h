// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_COREBUS_TYPES_H
#define GGL_COREBUS_TYPES_H

/// Maximum length of name of core bus interface.
#define GGL_INTERFACE_NAME_MAX_LEN 50

/// Socket path prefix for core bus sockets.
#define GGL_INTERFACE_SOCKET_PREFIX "/tmp/greengrass/"
/// Length of socket path prefix for core bus sockets.
#define GGL_INTERFACE_SOCKET_PREFIX_LEN \
    (sizeof(GGL_INTERFACE_SOCKET_PREFIX) - 1)

typedef enum {
    GGL_CORE_BUS_NOTIFY,
    GGL_CORE_BUS_CALL,
    GGL_CORE_BUS_SUBSCRIBE,
} GglCoreBusRequestType;

#endif
