#include "ggconfig.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

const char *test_key = "component/foo/bar";
const char *test_value = "baz";

void testInsert() {
    if (ggconfig_insert_key_and_value(test_key, test_value) != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
        exit(1);
    }
}

void testGet() {
    char buffer[4] = { 0 };
    size_t buffer_length = sizeof(buffer);

    if (ggconfig_get_value_from_key(test_key, buffer, &buffer_length)
        == GGL_ERR_OK) {
        if (buffer_length == strnlen(test_value, sizeof(buffer))
            && strncmp(test_value, buffer, buffer_length) == 0) {
            GGL_LOGI(
                "ggconfig test",
                "Value inserted into key and read back from key.  Success!"
            );
        }
    } else {
        printf("get failure\n");
        exit(1);
    }
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    if (GGL_ERR_OK != ggconfig_open()) {
        GGL_LOGE("ggconfig test", "ggconfig_open fail");
        exit(0);
    }

    testInsert();
    testGet();

    if (GGL_ERR_OK != ggconfig_close()) {
        GGL_LOGE("ggconfig test", "ggconfig_close fail");
    }

    return 0;
}
