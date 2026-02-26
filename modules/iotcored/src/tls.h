// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef IOTCORED_TLS_H
#define IOTCORED_TLS_H

#include <gg/error.h>
#include <gg/types.h>
#include <iotcored.h>
#include <stdbool.h>

typedef struct IotcoredTlsCtx IotcoredTlsCtx;

GgError iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx);

GgError iotcored_tls_read(IotcoredTlsCtx *ctx, GgBuffer *buf);
GgError iotcored_tls_write(
    IotcoredTlsCtx *ctx, GgBuffer buf, bool *has_pending
);

/// Get the underlying socket fd for use with poll().
/// Returns -1 if not connected.
int iotcored_tls_get_fd(IotcoredTlsCtx *ctx);

/// Check if the TLS layer has buffered data ready to read without polling.
bool iotcored_tls_read_ready(IotcoredTlsCtx *ctx);

void iotcored_tls_cleanup(IotcoredTlsCtx *ctx);

#endif
