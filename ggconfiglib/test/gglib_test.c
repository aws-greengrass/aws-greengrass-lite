#include "../src/ggconfig.h"
#include "ggl/object.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

static void test_insert(const char *test_key, const char *test_value) {
    GglBuffer test_key_buf
        = { .data = (uint8_t *) test_key, .len = strlen(test_key) };
    GglBuffer test_value_buf
        = { .data = (uint8_t *) test_value, .len = strlen(test_value) };

    if (ggconfig_write_value_at_key(&test_key_buf, &test_value_buf)
        != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
        assert(0);
    }
}

static void test_get(const char *test_key, const char *test_value) {
    GglBuffer key = { .data = (uint8_t *) test_key, .len = strlen(test_key) };
    GglBuffer test
        = { .data = (uint8_t *) test_value, .len = strlen(test_value) };
    char buffer[50] = { 0 };
    GglBuffer value = { .data = buffer, .len = sizeof(buffer) };

    if (ggconfig_get_value_from_key(&key, &value) == GGL_ERR_OK) {
        GGL_LOGI("test_get", "received %s", buffer);
        if (ggl_buffer_eq(value, test)) {
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
    GglBuffer test_key1 = GGL_STR("Foo/bar/Baz");
    GglBuffer test_key2 = GGL_STR("foo/bar/baz");
    GglBuffer value1 = GGL_STR("aValue");
    GglBuffer value2 = GGL_STR("anotherValue");
    GglError error1 = ggconfig_write_value_at_key(&test_key1, &value1);
    GglError error2 = ggconfig_write_value_at_key(&test_key2, &value2);

    assert(error1 == GGL_ERR_OK && error2 == GGL_ERR_OK);
    GGL_LOGI("ggconfig test", "case insensitivity test pass");
}

static void test_insert_bad_key(void) {
    GglBuffer test_keys[] = { GGL_STR("the key path"),
                              GGL_STR("/key/path\\test"),
                              GGL_STR("key/path\\test"),
                              GGL_STR("key/1path/a_test") };
    GglBuffer value1 = GGL_STR("aValue");

    for (unsigned int index = 0; index < sizeof(test_keys) / sizeof(*test_keys);
         index++) {
        if (ggconfig_write_value_at_key(&test_keys[index], &value1)
            == GGL_ERR_INVALID) {
            GGL_LOGI(
                "ggconfig test",
                "bad path detected : %.*s",
                (int) test_keys[index].len,
                (char *) test_keys[index].data
            );
        } else {
            GGL_LOGE(
                "ggconfig test",
                "bad path not detected: %.*s",
                (int) test_keys[index].len,
                (char *) test_keys[index].data
            );
            assert(0);
        }
    }
}

static void test_get_with_bad_key(void) {
    GglBuffer test_key = GGL_STR("one/bad/key");
    uint8_t value_buffer[64] = { 0 };
    GglBuffer value = { .data = value_buffer, .len = sizeof(value_buffer) };

    GglError error = ggconfig_get_value_from_key(&test_key, &value);
    if (error == GGL_ERR_OK) {
        GGL_LOGE(
            "ggconfig_test",
            "Found %.*s at %.*s",
            (int) value.len,
            (char *) value.data,
            (int) test_key.len,
            (char *) test_key.data
        );
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
