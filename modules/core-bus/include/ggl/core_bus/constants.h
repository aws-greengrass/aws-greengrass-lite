// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_COREBUS_CONSTANTS_H
#define GGL_COREBUS_CONSTANTS_H

//! Core Bus constants

/// Maximum size of core-bus packet.
/// Can be configured with `-DGGL_IPC_MAX_MSG_LEN=<N>`.
#ifndef GGL_COREBUS_MAX_MSG_LEN
#define GGL_COREBUS_MAX_MSG_LEN 10000
#endif

#endif
