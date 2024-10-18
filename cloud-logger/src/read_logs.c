#include "cloud_logger.h"
#include <ggl/alloc.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stdio.h>
#include <stdlib.h>

int read_log_100(long index, GglObjVec *lists_obj, GglAlloc *alloc) {
    // Command to fetch all journalctl logs
    const char *cmd = "journalctl --no-pager --reverse";

    // Open a process by creating a pipe, fork(), and invoking the shell
    FILE *fp = popen(cmd, "r");
    if (fp == NULL) {
        perror("popen failed");
        return 1;
    }

    // Buffer to hold each line of output
    char line[2048];
    int count = 0; // To keep track of the number of lines read

    int a = 0;
    // Read the output line by line
    while (a < 100) {
        if (fgets(line, sizeof(line), fp) == NULL) {
            break;
        }
        count++;

        // Skip the first 5 lines
        if (count > index) {
            ggl_obj_vec_push(lists_obj, GGL_OBJ(GGL_STR(line)));
            a++;
        }
    }

    // Close the process
    if (pclose(fp) == -1) {
        perror("pclose failed");
        return 1;
    }

    GglObject out_object;
    out_object.list = lists_obj->list;
    ggl_obj_deep_copy(&out_object, alloc);

    return 0;
}
