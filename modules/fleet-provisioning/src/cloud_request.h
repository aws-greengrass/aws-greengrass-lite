// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_FLEETPROV_CLOUD_REQUEST_H
#define GGL_FLEETPROV_CLOUD_REQUEST_H

#include <gg/error.h>
#include <gg/types.h>

GgError ggl_get_certificate_from_aws(
    GgBuffer csr_as_ggl_buffer,
    GgBuffer template_name,
    GgMap template_params,
    GgBuffer *thing_name_out,
    int certificate_fd
);

#endif
