#include "priv_io.h"
#include "ggl/io.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <stddef.h>

static GglError priv_file_write(void *ctx, GglBuffer buf) {
    if (buf.len == 0) {
        return GGL_ERR_OK;
    }
    if (ctx == NULL) {
        return GGL_ERR_NOMEM;
    }
    FileWriterContext *context = ctx;
    return ggl_file_write(context->fd, buf);
}

GglWriter priv_file_writer(FileWriterContext *ctx) {
    return (GglWriter) { .write = priv_file_write, .ctx = ctx };
}
