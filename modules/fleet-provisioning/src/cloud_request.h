// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FLEETPROV_CLOUD_REQUEST_H
#define GGL_FLEETPROV_CLOUD_REQUEST_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError ggl_get_certificate_from_aws(
    GglBuffer csr_as_ggl_buffer,
    GglBuffer template_name,
    GglMap template_params,
    GglBuffer *thing_name_out,
    int certificate_fd
);

#endif
