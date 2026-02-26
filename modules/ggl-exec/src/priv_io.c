#include "priv_io.h"
#include <gg/error.h>
#include <gg/file.h>
#include <gg/io.h>
#include <gg/types.h>
#include <stddef.h>

static GgError priv_file_write(void *ctx, GgBuffer buf) {
    if (buf.len == 0) {
        return GG_ERR_OK;
    }
    if (ctx == NULL) {
        return GG_ERR_NOMEM;
    }
    FileWriterContext *context = ctx;
    return gg_file_write(context->fd, buf);
}

GgWriter priv_file_writer(FileWriterContext *ctx) {
    return (GgWriter) { .write = priv_file_write, .ctx = ctx };
}
