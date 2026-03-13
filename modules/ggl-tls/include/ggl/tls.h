// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_TLS_H
#define GGL_TLS_H

//! Pluggable TLS connection interface

#include <gg/error.h>
#include <gg/types.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct {
    int fd;
    pid_t pid;
} GglTlsConn;

/// Establish a mutual TLS connection.
GgError ggl_tls_connect(
    const char *hostname,
    uint16_t port,
    const char *private_key,
    const char *certificate,
    const char *root_ca,
    GglTlsConn *conn
);

/// Read from the TLS tunnel.
GgError ggl_tls_read(GglTlsConn *conn, GgBuffer *buf);

/// Write to the TLS tunnel.
GgError ggl_tls_write(GglTlsConn *conn, GgBuffer buf);

/// Get the underlying fd for use with poll().
int ggl_tls_get_fd(GglTlsConn *conn);

/// Close the tunnel and clean up the helper process.
GgError ggl_tls_close(GglTlsConn *conn);

#endif
