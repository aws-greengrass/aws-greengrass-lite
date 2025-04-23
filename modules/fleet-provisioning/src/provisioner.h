// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef PROVISIONER_H
#define PROVISIONER_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <sys/types.h>

GglError make_request(
    GglBuffer csr_as_ggl_buffer, GglBuffer cert_file_path, pid_t iotcored_pid
);

#endif
