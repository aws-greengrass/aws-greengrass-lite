// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IOT_CORE_CALL_H
#define GGL_IOT_CORE_CALL_H

//! Helper for calling AWS IoT Core APIs

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError ggl_aws_iot_call(
    GglBuffer topic, GglObject payload, GglAlloc *alloc, GglObject *result
);

#endif
