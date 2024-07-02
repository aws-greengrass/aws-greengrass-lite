#include "ggconfig.h"
#include <stdio.h>

const char *testKey = "component/foo/bar";
const char *testValue = "baz";

int main(int argc, char **argv) {
    if (ggconfig_insertKeyAndValue(testKey, testValue) == GGL_ERR_OK) {
        char buffer[4];
        if (ggconfig_getValueFromKey(testKey, buffer, sizeof(buffer))
            == GGL_ERR_OK) {
            if (strncmp(testValue, buffer, sizeof(buffer)) == 0) {
                printf(
                    "Value inserted into key and read back from key.  Success!"
                )
            }
        }
    }

    return 0;
}