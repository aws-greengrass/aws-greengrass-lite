// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "helpers.h"
#include <ggl/object.h>
#include <string.h>

#define PATH_STRING_MAX_SIZE 1024

/// @brief convert a list of buffers into a string that represents a path
/// this function is not thread safe and assumes the list is full of buffers
/// @param key_path a list of buffers that will be rendered into a null
/// terminated string has a fixed maximum length and will end in /... if it is
/// too small.
/// @return a statically allocated path_string suitable for debug printing
char *print_key_path(GglList *key_path) {
    static char path_string[PATH_STRING_MAX_SIZE] = { 0 };
    size_t string_length = 0;
    memset(path_string, 0, sizeof(path_string));
    for (size_t x = 0; x < key_path->len; x++) {
        size_t additional_length = 1 + key_path->items[x].buf.len;
        if (5 + string_length + additional_length < sizeof(path_string)) {
            if (x > 0) {
                strncat(path_string, "/ ", 1);
            }
            strncat(
                path_string,
                (char *) key_path->items[x].buf.data,
                key_path->items[x].buf.len
            );
        } else {
            strncat(path_string, (char *) "/... ", 4);
            break;
        }
    }
    return path_string;
}
