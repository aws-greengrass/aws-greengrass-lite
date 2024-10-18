#include "cloud_logger.h"
#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <stdio.h>

GglError read_log(FILE *fp, GglObjVec *filling, GglAlloc *alloc) {
    const size_t VALUE_LENGTH = 2048;
    time_t start;
    time_t now;
    double time_diff;

    // Get the start time
    time(&start);

    uint8_t *line = GGL_ALLOCN(alloc, uint8_t, VALUE_LENGTH);
    if (!line) {
        GGL_LOGE("no more memory to allocate");
        return GGL_ERR_NOMEM;
    }

    // Read the output line by line
    while (filling->list.len < filling->capacity) {
        time(&now);
        // Calculate the time difference in seconds
        time_diff = difftime(now, start);
        if (time_diff > 11.0) {
            break;
        }

        if (fgets((char *) line, (int) VALUE_LENGTH, fp) == NULL) {
            continue;
        }

        GglObject value;
        value.type = GGL_TYPE_BUF;
        value.buf.data = line;
        value.buf.len = strnlen((char *) line, VALUE_LENGTH);

        ggl_obj_vec_push(filling, value);
    }

    // TODO:: clean the resource when terminating
    // if (pclose(fp) == -1) {
    //     perror("pclose failed");
    //     return 1;
    // }

    return GGL_ERR_OK;
}
