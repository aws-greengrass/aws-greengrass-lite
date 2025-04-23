// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGL_IO_H
#define GGL_IO_H

//! Reader/Writer abstractions

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <stdio.h>

/// Abstraction for streaming data into
typedef struct {
    GglError (*write)(void *ctx, GglBuffer buf);
    void *ctx;
} GglWriter;

/// Write to a GglWriter
static inline GglError ggl_writer_call(GglWriter writer, GglBuffer buf) {
    if (writer.write == NULL) {
        return buf.len == 0 ? GGL_ERR_OK : GGL_ERR_FAILURE;
    }
    return writer.write(writer.ctx, buf);
}

/// Writer that 0 bytes can be written to
static const GglWriter GGL_NULL_WRITER = { 0 };

/// Abstraction for streaming data from
/// Updates buf to amount read
/// `read` must fill the buffer as much as possible; if less than buf's original
/// length is read, the data is complete.
/// APIs may require a reader that errors if the data does not fit into buffer
/// or may require a reader that can be read multiple times until complete.
typedef struct {
    GglError (*read)(void *ctx, GglBuffer *buf);
    void *ctx;
} GglReader;

/// Read from a GglReader
static inline GglError ggl_reader_call(GglReader reader, GglBuffer *buf) {
    if (reader.read == NULL) {
        buf->len = 0;
        return GGL_ERR_OK;
    }
    return reader.read(reader.ctx, buf);
}

/// Fill entire buffer from a GglReader
static inline GglError ggl_reader_call_exact(GglReader reader, GglBuffer buf) {
    GglBuffer copy = buf;
    GglError ret = ggl_reader_call(reader, &copy);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    return copy.len == buf.len ? GGL_ERR_OK : GGL_ERR_FAILURE;
}

/// Reader that 0 bytes can be read from
static const GglReader GGL_NULL_READER = { 0 };

/// Returns a writer that writes into a buffer, consuming used portion
GglWriter ggl_buf_writer(GglBuffer *buf);

#endif
