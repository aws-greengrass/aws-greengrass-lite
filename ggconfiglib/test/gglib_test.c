#include "ggconfig.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

void testInsert(const char *test_key, const char *test_value) {
    if (ggconfig_write_value_at_key(test_key, test_value) != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
        exit(1);
    }
}

void testGet(const char *test_key, const char *test_value) {
    char buffer[50] = { 0 };
    int buffer_length = sizeof(buffer);

    if (ggconfig_get_value_from_key(test_key, buffer, &buffer_length)
        == GGL_ERR_OK) {
        GGL_LOGI("testGet", "received %s", buffer);
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

void testCaseSensitiveKeys() {
    const char *testKeys[] = { "Foo/bar/Baz", "foo/bar/baz" };
    GglError error1 = ggconfig_write_value_at_key(testKeys[0], "aValue");
    GglError error2 = ggconfig_write_value_at_key(testKeys[1], "anotherValue");

    if (error1 == GGL_ERR_OK && error2 == GGL_ERR_OK) {
        GGL_LOGI("gglconfig test", "case insensitivity test pass");
    } else {
        GGL_LOGE("gglconfig test", "case insensitivity test fail");
        exit(1);
    }
}

void testInsertBadKey() {
    const char *testKeys[] = {
        "the key path", "/key/path\\test", "key/path\\test", "key/1path/a_test"
    };

    for (int index = 0; index < sizeof(testKeys) / sizeof(*testKeys); index++) {
        if (ggconfig_write_value_at_key(testKeys[index], "aValue")
            == GGL_ERR_INVALID) {
            GGL_LOGI(
                "ggconfig test", "bad path detected : %s", testKeys[index]
            );
        } else {
            GGL_LOGE(
                "gglconfig test", "bad path not detected: %s", testKeys[index]
            );
            exit(1);
        }
    }
}

void testGetWithBadKey() {
    const char *testKey = "one/bad/key";
    char valueBuffer[64] = { 0 };
    int valueBufferLength = sizeof(valueBuffer);

    GglError error
        = ggconfig_get_value_from_key(testKey, valueBuffer, &valueBufferLength);
    if (error == GGL_ERR_OK) {
        GGL_LOGE("gglconfig_test", "Found %s at %s", valueBuffer, testKey);
        exit(1);
    }
    GGL_LOGI("gglconfig_test", "bad key get successful");
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    if (GGL_ERR_OK != ggconfig_open()) {
        GGL_LOGE("ggconfig test", "ggconfig_open fail");
        exit(0);
    }

    testInsertBadKey();
    testGetWithBadKey();
    testCaseSensitiveKeys();
    testInsert("component/foo/bar", "another big value");
    testInsert("component/fooer/bar", "value2");
    testInsert("component/foo/baz", "value");
    testInsert("global", "value");

    testGet("component/foo/bar", "another big value");

    if (GGL_ERR_OK != ggconfig_close()) {
        GGL_LOGE("ggconfig test", "ggconfig_close fail");
    }

    return 0;
}
