#include "ggconfig.h"
#include <stdio.h>

const char *test_key = "component/foo/bar";
const char *test_value = "baz";

int main(int argc, char **argv) {
    if (ggconfig_insert_key_and_value(test_key, test_value) == GGL_ERR_OK) {
        char buffer[4];
        if (ggconfig_get_value_from_key(test_key, buffer, sizeof(buffer))
            == GGL_ERR_OK) {
            if (strncmp(test_value, buffer, sizeof(buffer)) == 0) {
                printf(
                    "Value inserted into key and read back from key.  Success!"
                )
            }
        }
    }

    return 0;
}
