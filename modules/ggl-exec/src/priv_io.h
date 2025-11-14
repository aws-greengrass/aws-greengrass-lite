#ifndef GGL_EXEC_PRIV_IO_H
#define GGL_EXEC_PRIV_IO_H

#include <gg/io.h>

typedef struct FileWriterContext {
    int fd;
} FileWriterContext;

GgWriter priv_file_writer(FileWriterContext *ctx);

#endif
