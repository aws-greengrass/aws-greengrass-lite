#include "ggconfig.h"
#include <ggl/error.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

const char *test_key = "component/foo/bar";
const char *test_value = "baz";

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    if (ggconfig_insert_key_and_value(test_key, test_value) == GGL_ERR_OK) {
        char buffer[4];
        size_t buffer_length = sizeof(buffer);

        if (ggconfig_get_value_from_key(test_key, buffer, &buffer_length)
            == GGL_ERR_OK) {
            if (strncmp(test_value, buffer, buffer_length) == 0) {
                printf(
                    "Value inserted into key and read back from key.  Success!"
                );
            }
        }
    }

    return 0;
}
