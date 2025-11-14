// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_PRIV_IO_H
#define GGDEPLOYMENTD_PRIV_IO_H

// TODO: move into ggl-sdk io.h

#include <gg/io.h>
#include <gg/vector.h>

// Appends content onto the back of a byte vector
// Writer function returns GG_ERR_NOMEM if append fails or if writer was
// created with NULL vec.
GgWriter priv_byte_vec_writer(GgByteVec *byte_vec);

#endif
