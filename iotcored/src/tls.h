// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_TLS_H
#define IOTCORED_TLS_H

#include "ggl/error.h"
#include "iotcored.h"
#include <ggl/buffer.h>

typedef struct IotcoredTlsCtx IotcoredTlsCtx;

GglError iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx);

GglError iotcored_tls_read(IotcoredTlsCtx *ctx, GglBuffer *buf);
GglError iotcored_tls_write(IotcoredTlsCtx *ctx, GglBuffer buf);

void iotcored_tls_cleanup(IotcoredTlsCtx *ctx);

#endif
