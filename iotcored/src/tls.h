/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef IOTCORED_TLS_H
#define IOTCORED_TLS_H

#include "args.h"
#include "ggl/object.h"

typedef struct IotcoredTlsCtx IotcoredTlsCtx;

int iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx);

int iotcored_tls_read(IotcoredTlsCtx *ctx, GglBuffer *buf);
int iotcored_tls_write(IotcoredTlsCtx *ctx, GglBuffer buf);

void iotcored_tls_cleanup(IotcoredTlsCtx *ctx);

#endif
