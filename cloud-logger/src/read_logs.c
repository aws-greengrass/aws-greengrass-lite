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
    time_t start;
    time_t now;
    double time_diff;

    // Get the start time
    time(&start);

    // Read the output line by line
    while (filling->list.len < filling->capacity) {
        time(&now);
        // Calculate the time difference in seconds
        time_diff = difftime(now, start);
        if (time_diff > 11.0) {
            break;
        }

        uint8_t *line = GGL_ALLOCN(alloc, uint8_t, MAX_LINE_LENGTH);
        if (!line) {
            // This should never happen because the alloc memory is defined as
            // MAX_LINE_LENGTH * filling->capacity
            GGL_LOGW("Ran out of memory for allocation. Returning early to "
                     "swap memory buffers.");
            break;
        }

        if (fgets((char *) line, (int) MAX_LINE_LENGTH, fp) == NULL) {
            continue;
        }

        GglObject value;
        value.type = GGL_TYPE_BUF;
        value.buf.data = line;
        value.buf.len = strnlen((char *) line, MAX_LINE_LENGTH);

        ggl_obj_vec_push(filling, value);
    }

    // TODO:: clean the resource when terminating
    // if (pclose(fp) == -1) {
    //     perror("pclose failed");
    //     return 1;
    // }

    return GGL_ERR_OK;
}
