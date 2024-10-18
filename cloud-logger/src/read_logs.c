#include "cloud_logger.h"
#include <sys/types.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

GglError read_log(FILE *fp, GglObjVec *filling, GglAlloc *alloc) {
    const size_t VALUE_LENGTH = 2048;

    uint8_t *line = GGL_ALLOCN(alloc, uint8_t, VALUE_LENGTH);
    if (!line) {
        GGL_LOGE("no more memory to allocate");
        return GGL_ERR_NOMEM;
    }

    // Read the output line by line
    while (filling->list.len < filling->capacity) {
        if (fgets((char *) line, (int) VALUE_LENGTH, fp) == NULL) {
            continue;
        }

        GglObject value;
        value.type = GGL_TYPE_BUF;
        value.buf.data = line;
        value.buf.len = strnlen((char *) line, VALUE_LENGTH);

        ggl_obj_vec_push(filling, value);
    }

    // Close the process
    // if (pclose(fp) == -1) {
    //     perror("pclose failed");
    //     return 1;
    // }

    return GGL_ERR_OK;
}
