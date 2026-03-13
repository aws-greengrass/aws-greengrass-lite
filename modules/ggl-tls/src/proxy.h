// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_TLS_PROXY_H
#define GGL_TLS_PROXY_H

#include <gg/error.h>
#include <stdint.h>

/// Open a TCP connection to hostname:port, tunneling through an HTTP proxy
/// if one is configured (via env vars or gg config store).
/// Returns the connected fd in *out_fd.
GgError proxy_connect(const char *hostname, uint16_t port, int *out_fd);

#endif
