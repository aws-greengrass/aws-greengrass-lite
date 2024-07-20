#include "ggconfig.h"
#include <assert.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

static void test_insert(const char *test_key, const char *test_value) {
    if (ggconfig_write_value_at_key(test_key, test_value) != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
        assert(0);
    }
}

static void test_get(const char *test_key, const char *test_value) {
    char buffer[50] = { 0 };
    size_t buffer_length = sizeof(buffer);

    if (ggconfig_get_value_from_key(test_key, buffer, (int *) &buffer_length)
        == GGL_ERR_OK) {
        GGL_LOGI("test_get", "received %s", buffer);
        if (buffer_length == strnlen(test_value, sizeof(buffer))
            && strncmp(test_value, buffer, buffer_length) == 0) {
            GGL_LOGI(
                "ggconfig test",
                "Value inserted into key and read back from key.  Success!"
            );
        }
    } else {
        printf("get failure\n");
        assert(0);
    }
}

static void test_case_sensitive_keys(void) {
    const char *test_keys[] = { "Foo/bar/Baz", "foo/bar/baz" };
    GglError error1 = ggconfig_write_value_at_key(test_keys[0], "aValue");
    GglError error2 = ggconfig_write_value_at_key(test_keys[1], "anotherValue");

    assert(error1 == GGL_ERR_OK && error2 == GGL_ERR_OK);
    GGL_LOGI("ggconfig test", "case insensitivity test pass");
}

static void test_insert_bad_key(void) {
    const char *test_keys[] = {
        "the key path", "/key/path\\test", "key/path\\test", "key/1path/a_test"
    };

    for (unsigned int index = 0; index < sizeof(test_keys) / sizeof(*test_keys);
         index++) {
        if (ggconfig_write_value_at_key(test_keys[index], "aValue")
            == GGL_ERR_INVALID) {
            GGL_LOGI(
                "ggconfig test", "bad path detected : %s", test_keys[index]
            );
        } else {
            GGL_LOGE(
                "ggconfig test", "bad path not detected: %s", test_keys[index]
            );
            assert(0);
        }
    }
}

static void test_get_with_bad_key(void) {
    const char *test_key = "one/bad/key";
    char value_buffer[64] = { 0 };
    int value_buffer_length = sizeof(value_buffer);

    GglError error = ggconfig_get_value_from_key(
        test_key, value_buffer, &value_buffer_length
    );
    if (error == GGL_ERR_OK) {
        GGL_LOGE("ggconfig_test", "Found %s at %s", value_buffer, test_key);
        assert(0);
    }
    GGL_LOGI("ggconfig_test", "bad key get successful");
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    if (GGL_ERR_OK != ggconfig_open()) {
        GGL_LOGE("ggconfig test", "ggconfig_open fail");
        assert(0);
    }

    test_insert_bad_key();
    test_get_with_bad_key();
    test_case_sensitive_keys();
    test_insert("component/foo/bar", "another big value");
    test_insert("component/bar/foo", "value2");
    test_insert("component/foo/baz", "value");
    test_insert("global", "value");

    test_get("component/foo/bar", "another big value");

    if (GGL_ERR_OK != ggconfig_close()) {
        GGL_LOGE("ggconfig test", "ggconfig_close fail");
    }

    return 0;
}
