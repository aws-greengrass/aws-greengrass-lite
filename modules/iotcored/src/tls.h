// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_TLS_H
#define IOTCORED_TLS_H

#include "iotcored.h"
#include <gg/buffer.h>
#include <gg/error.h>

typedef struct IotcoredTlsCtx IotcoredTlsCtx;

GgError iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx);

GgError iotcored_tls_read(IotcoredTlsCtx *ctx, GgBuffer *buf);
GgError iotcored_tls_write(IotcoredTlsCtx *ctx, GgBuffer buf);

void iotcored_tls_cleanup(IotcoredTlsCtx *ctx);

#endif
