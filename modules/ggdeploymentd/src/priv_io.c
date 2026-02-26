// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "priv_io.h"
#include <gg/error.h>
#include <gg/io.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <stddef.h>

static GgError byte_vec_write(void *ctx, GgBuffer buf) {
    if (buf.len == 0) {
        return GG_ERR_OK;
    }
    if (ctx == NULL) {
        return GG_ERR_NOMEM;
    }
    GgByteVec *byte_vec = (GgByteVec *) ctx;
    return gg_byte_vec_append(byte_vec, buf);
}

GgWriter priv_byte_vec_writer(GgByteVec *byte_vec) {
    return (GgWriter) { .write = byte_vec_write, .ctx = byte_vec };
}
