#ifndef GGL_EXEC_PRIV_IO_H
#define GGL_EXEC_PRIV_IO_H

#include <ggl/io.h>

typedef struct FileWriterContext {
    int fd;
} FileWriterContext;

GglWriter priv_file_writer(FileWriterContext *ctx);

#endif
