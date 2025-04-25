#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>

static char *print_key_path(GglList key_path) {
    static char path_string[64] = { 0 };
    memset(path_string, 0, sizeof(path_string));
    for (size_t x = 0; x < key_path.len; x++) {
        if (x > 0) {
            strncat(path_string, "/ ", 1);
        }
        strncat(
            path_string,
            (char *) ggl_obj_into_buf(key_path.items[x]).data,
            ggl_obj_into_buf(key_path.items[x]).len
        );
    }
    return path_string;
}

// a timestamp of -1 means no timestamp will be sent
static void test_insert(
    GglList test_key,
    GglObject test_value,
    int64_t timestamp,
    GglError expected_result
) {
    GGL_LOGD(
        "test_insert: key=%s, timestamp=%d, expected_result=%s",
        print_key_path(test_key),
        (int) timestamp,
        ggl_strerror(expected_result)
    );
    GglBuffer server = GGL_STR("gg_config");

    static uint8_t alloc_mem[4096];
    GglArena alloc = ggl_arena_init(GGL_BUF(alloc_mem));

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"), ggl_obj_list(test_key) },
        { GGL_STR("value"), test_value },
        { GGL_STR("timestamp"), ggl_obj_i64(timestamp) }
    );
    if (timestamp < 0) {
        params.len -= 1;
    }

    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    GglObject result;

    GglError remote_error = GGL_ERR_OK;
    GglError error = ggl_call(
        server, GGL_STR("write"), params, &remote_error, &alloc, &result
    );

    if (expected_result != GGL_ERR_OK && error != GGL_ERR_REMOTE) {
        GGL_LOGE(
            "insert of key %s expected error %s but there was not a remote "
            "error",
            print_key_path(test_key),
            ggl_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GGL_ERR_OK && error != GGL_ERR_OK) {
        GGL_LOGE(
            "insert of key %s did not expect error but got error %s and remote "
            "error %s",
            print_key_path(test_key),
            ggl_strerror(error),
            ggl_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GGL_LOGE(
            "insert of key %s expected remote error %s but got %s",
            print_key_path(test_key),
            ggl_strerror(expected_result),
            ggl_strerror(remote_error)
        );
        assert(0);
    }
}

static void compare_objects(GglObject expected, GglObject result);

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_lists(GglList expected, GglList result) {
    if (result.len != expected.len) {
        GGL_LOGE(
            "expected list of length %d got %d",
            (int) expected.len,
            (int) result.len
        );
        return;
    }
    for (size_t i = 0; i < expected.len; i++) {
        GglObject expected_item = expected.items[i];
        GglObject result_item = result.items[i];
        compare_objects(expected_item, result_item);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_maps(GglMap expected, GglMap result) {
    if (result.len != expected.len) {
        GGL_LOGE(
            "expected map of length %d got %d",
            (int) expected.len,
            (int) result.len
        );
        return;
    }
    for (size_t i = 0; i < expected.len; i++) {
        GglBuffer expected_key = expected.pairs[i].key;
        GglObject expected_val = expected.pairs[i].val;
        bool found = false;
        for (size_t j = 0; j < result.len; j++) {
            if (strncmp(
                    (const char *) expected_key.data,
                    (const char *) result.pairs[j].key.data,
                    expected_key.len
                )
                == 0) {
                found = true;
                GglObject result_item = result.pairs[j].val;
                compare_objects(expected_val, result_item);
                break;
            }
        }
        if (!found) {
            GGL_LOGE(
                "expected key %.*s not found",
                (int) expected_key.len,
                (char *) expected_key.data
            );
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_objects(GglObject expected, GglObject result) {
    switch (ggl_obj_type(expected)) {
    case GGL_TYPE_BOOLEAN:
        if (ggl_obj_type(result) != GGL_TYPE_BOOLEAN) {
            GGL_LOGE("expected boolean, got %d", ggl_obj_type(result));
            return;
        }
        if (ggl_obj_into_bool(result) != ggl_obj_into_bool(expected)) {
            GGL_LOGE(
                "expected %d got %d",
                ggl_obj_into_bool(expected),
                ggl_obj_into_bool(result)
            );
        }
        break;
    case GGL_TYPE_I64:
        if (ggl_obj_type(result) != GGL_TYPE_I64) {
            GGL_LOGE("expected i64, got %d", ggl_obj_type(result));
            return;
        }
        if (ggl_obj_into_i64(result) != ggl_obj_into_i64(expected)) {
            GGL_LOGE(
                "expected %" PRId64 " got %" PRId64,
                ggl_obj_into_i64(expected),
                ggl_obj_into_i64(result)
            );
        }
        break;
    case GGL_TYPE_F64:
        if (ggl_obj_type(result) != GGL_TYPE_F64) {
            GGL_LOGE("expected f64, got %d", ggl_obj_type(result));
            return;
        }
        if (ggl_obj_into_f64(result) != ggl_obj_into_f64(expected)) {
            GGL_LOGE(
                "expected %f got %f",
                ggl_obj_into_f64(expected),
                ggl_obj_into_f64(result)
            );
        }
        break;
    case GGL_TYPE_BUF:
        if (ggl_obj_type(result) != GGL_TYPE_BUF) {
            GGL_LOGE("expected buffer, got %d", ggl_obj_type(result));
            return;
        }
        if (strncmp(
                (const char *) ggl_obj_into_buf(result).data,
                (const char *) ggl_obj_into_buf(expected).data,
                ggl_obj_into_buf(result).len
            )
            != 0) {
            GGL_LOGE(
                "expected %.*s got %.*s",
                (int) ggl_obj_into_buf(expected).len,
                (char *) ggl_obj_into_buf(expected).data,
                (int) ggl_obj_into_buf(result).len,
                (char *) ggl_obj_into_buf(result).data
            );
            return;
        }
        break;
    case GGL_TYPE_LIST:
        if (ggl_obj_type(result) != GGL_TYPE_LIST) {
            GGL_LOGE("expected list, got %d", ggl_obj_type(result));
            return;
        }
        compare_lists(ggl_obj_into_list(expected), ggl_obj_into_list(result));
        break;
    case GGL_TYPE_MAP:
        if (ggl_obj_type(result) != GGL_TYPE_MAP) {
            GGL_LOGE("expected map, got %d", ggl_obj_type(result));
            return;
        }
        compare_maps(ggl_obj_into_map(expected), ggl_obj_into_map(result));
        break;
    case GGL_TYPE_NULL:
        if (ggl_obj_type(result) != GGL_TYPE_NULL) {
            GGL_LOGE("expected null, got %d", ggl_obj_type(result));
            return;
        }
        break;
    default:
        GGL_LOGE("unexpected type %d", ggl_obj_type(expected));
        break;
    }
}

static void test_get(
    GglList test_key_path, GglObject expected_object, GglError expected_result
) {
    GGL_LOGD(
        "test_get %s, expecting %s",
        print_key_path(test_key_path),
        ggl_strerror(expected_result)
    );
    GglBuffer server = GGL_STR("gg_config");
    static uint8_t alloc_mem[4096];
    GglArena alloc = ggl_arena_init(GGL_BUF(alloc_mem));

    GglMap params
        = GGL_MAP({ GGL_STR("key_path"), ggl_obj_list(test_key_path) }, );
    GglObject result;

    GglError remote_error = GGL_ERR_OK;
    GglError error = ggl_call(
        server, GGL_STR("read"), params, &remote_error, &alloc, &result
    );
    if (expected_result != GGL_ERR_OK && error != GGL_ERR_REMOTE) {
        GGL_LOGE(
            "get key %s expected result %s but there was not a remote error",
            print_key_path(test_key_path),
            ggl_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GGL_ERR_OK && error != GGL_ERR_OK) {
        GGL_LOGE(
            "get key %s did not expect error but got error %s and remote "
            "error %s",
            print_key_path(test_key_path),
            ggl_strerror(error),
            ggl_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GGL_LOGE(
            "get key %s expected result %s but got %s",
            print_key_path(test_key_path),
            ggl_strerror(expected_result),
            ggl_strerror(remote_error)
        );
        assert(0);
        return;
    }
    if (expected_result == GGL_ERR_OK) {
        compare_objects(expected_object, result);
    }
}

static void test_list(
    GglList test_key_path, GglObject expected_object, GglError expected_result
) {
    GGL_LOGD(
        "test_list %s, expecting %s",
        print_key_path(test_key_path),
        ggl_strerror(expected_result)
    );
    GglBuffer server = GGL_STR("gg_config");
    static uint8_t alloc_mem[4096];
    GglArena alloc = ggl_arena_init(GGL_BUF(alloc_mem));

    GglMap params
        = GGL_MAP({ GGL_STR("key_path"), ggl_obj_list(test_key_path) }, );
    GglObject result;

    GglError remote_error = GGL_ERR_OK;
    GglError error = ggl_call(
        server, GGL_STR("list"), params, &remote_error, &alloc, &result
    );
    if (expected_result != GGL_ERR_OK && error != GGL_ERR_REMOTE) {
        GGL_LOGE(
            "list key %s expected result %s but there was not a remote error",
            print_key_path(test_key_path),
            ggl_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GGL_ERR_OK && error != GGL_ERR_OK) {
        GGL_LOGE(
            "list key %s did not expect error but got error %s and remote "
            "error %s",
            print_key_path(test_key_path),
            ggl_strerror(error),
            ggl_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GGL_LOGE(
            "list key %s expected result %s but got %s",
            print_key_path(test_key_path),
            ggl_strerror(expected_result),
            ggl_strerror(remote_error)
        );
        assert(0);
        return;
    }
    if (expected_result == GGL_ERR_OK) {
        compare_objects(expected_object, result);
    }
}

static void test_delete(GglList key_path, GglError expected_result) {
    GGL_LOGD(
        "test_delete %s, expecting %s",
        print_key_path(key_path),
        ggl_strerror(expected_result)
    );
    GglBuffer server = GGL_STR("gg_config");

    GglMap params = GGL_MAP({ GGL_STR("key_path"), ggl_obj_list(key_path) }, );

    GglError remote_error = GGL_ERR_OK;
    GglError error = ggl_call(
        server, GGL_STR("delete"), params, &remote_error, NULL, NULL
    );
    if (expected_result != GGL_ERR_OK && error != GGL_ERR_REMOTE) {
        GGL_LOGE(
            "delete key %s expected result %s but there was not a remote error",
            print_key_path(key_path),
            ggl_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GGL_ERR_OK && error != GGL_ERR_OK) {
        GGL_LOGE(
            "delete key %s did not expect error but got error %s and remote "
            "error %s",
            print_key_path(key_path),
            ggl_strerror(error),
            ggl_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GGL_LOGE(
            "delete key %s expected result %s but got %s",
            print_key_path(key_path),
            ggl_strerror(expected_result),
            ggl_strerror(remote_error)
        );
        assert(0);
        return;
    }
}

static GglError subscription_callback(
    void *ctx, unsigned int handle, GglObject data
) {
    (void) ctx;
    (void) data;
    GGL_LOGI("Subscription callback called for handle %d.", handle);
    if (ggl_obj_type(data) == GGL_TYPE_LIST) {
        GGL_LOGI("read %s", print_key_path(ggl_obj_into_list(data)));
    } else {
        GGL_LOGE("expected a list ");
    }
    return GGL_ERR_OK;
}

static void subscription_close(void *ctx, unsigned int handle) {
    (void) ctx;
    (void) handle;
    GGL_LOGI("called");
}

static void test_subscribe(GglList key, GglError expected_response) {
    GGL_LOGD(
        "test_subscribe %s, expecting %s",
        print_key_path(key),
        ggl_strerror(expected_response)
    );
    GglBuffer server = GGL_STR("gg_config");

    GglMap params = GGL_MAP({ GGL_STR("key_path"), ggl_obj_list(key) }, );
    uint32_t handle = 0;
    GglError remote_error = GGL_ERR_OK;
    GglError error = ggl_subscribe(
        server,
        GGL_STR("subscribe"),
        params,
        subscription_callback,
        subscription_close,
        NULL,
        &remote_error,
        &handle
    );
    if (expected_response != GGL_ERR_OK && error != GGL_ERR_REMOTE) {
        GGL_LOGE(
            "subscribe key %s expected result %d but there was not a remote "
            "error",
            print_key_path(key),
            (int) expected_response
        );
        assert(0);
    }
    if (expected_response == GGL_ERR_OK && error != GGL_ERR_OK) {
        GGL_LOGE(
            "insert of key %s did not expect error but got error %s and remote "
            "error %s",
            print_key_path(key),
            ggl_strerror(error),
            ggl_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_response) {
        GGL_LOGE(
            "subscribe key %s expected result %s but got %s",
            print_key_path(key),
            ggl_strerror(expected_response),
            ggl_strerror(error)
        );
        assert(0);
        return;
    }
    if (error == GGL_ERR_OK) {
        GGL_LOGI("Success! key: %s handle: %d", print_key_path(key), handle);
    }
}

/*
test case for test_write_object
component = "component"
key_path = ["foobar"]
value = {
    "foo": {
        "bar": {
            "baz": [
                1,
                2,
                3,
                4
            ],
            "qux": 1
        },
        "quux": "string"
    },
    "corge": true,
    "grault": false
}
timestamp = 1723142212
*/

static void test_write_object(void) {
    GglBuffer test_key_path_json = GGL_STR("[\"component\",\"foobar\"]");
    GglBuffer test_value_json
        = GGL_STR("{\"foo\":{\"bar\":{\"baz\":[ 1,2,3,4],\"qux\":1},\"quux\": "
                  "\"string\" },\"corge\" : true, \"grault\" : false}");
    GglObject test_key_path_object;
    GglObject test_value_object;
    static uint8_t big_buffer[4096];
    GGL_LOGI("test begun");

    GglArena arena = ggl_arena_init(GGL_BUF(big_buffer));
    GglError ret = ggl_json_decode_destructive(
        test_key_path_json, &arena, &test_key_path_object
    );
    GGL_LOGI("json decode complete %d", ret);

    // Needs error checking?
    (void
    ) ggl_json_decode_destructive(test_value_json, &arena, &test_value_object);

    if (ggl_obj_type(test_key_path_object) == GGL_TYPE_LIST) {
        GGL_LOGI("found a list in the json path");
    } else {
        GGL_LOGE("json path is not a list");
    }

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"), test_key_path_object },
        { GGL_STR("value"), test_value_object }
    );
    ret = ggl_notify(GGL_STR("gg_config"), GGL_STR("write"), params);
    GGL_LOGI("test complete %d", ret);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    // Test to ensure getting a key which doesn't exist works
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component")),
            ggl_obj_buf(GGL_STR("nonexistent"))
        ),
        ggl_obj_map((GglMap) { 0 }),
        GGL_ERR_NOENTRY
    );

    // Test to ensure recursive/object write and read works
    test_write_object();
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component")),
            ggl_obj_buf(GGL_STR("foobar")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("qux"))
        ),
        ggl_obj_i64(1),
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component")),
            ggl_obj_buf(GGL_STR("foobar")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("baz"))
        ),
        ggl_obj_list(GGL_LIST(
            ggl_obj_i64(1), ggl_obj_i64(2), ggl_obj_i64(3), ggl_obj_i64(4)
        )),
        GGL_ERR_OK
    );

    GglObject bar = ggl_obj_map(GGL_MAP(
        { GGL_STR("qux"), ggl_obj_i64(1) },
        { GGL_STR("baz"),
          ggl_obj_list(GGL_LIST(
              ggl_obj_i64(1), ggl_obj_i64(2), ggl_obj_i64(3), ggl_obj_i64(4)
          )) }
    ));

    GglObject foo = ggl_obj_map(GGL_MAP(
        { GGL_STR("bar"), bar },
        { GGL_STR("quux"), ggl_obj_buf(GGL_STR("string")) }
    ));

    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component")), ggl_obj_buf(GGL_STR("foobar")),
        ),
        ggl_obj_map(GGL_MAP(
            { GGL_STR("foo"), foo },
            { GGL_STR("corge"), ggl_obj_bool(true) },
            { GGL_STR("grault"), ggl_obj_bool(false) },
        )),
        GGL_ERR_OK
    );

    // Test to ensure a key which is a value can't become a parent as well
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component1")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component1")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value1")),
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component1")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("subkey"),
                              ggl_obj_buf(GGL_STR("value2")) })),
        -1,
        GGL_ERR_FAILURE // expect failure because `component/foo/bar/key` is
                        // already a value, so it should not also be a parent of
                        // a subkey
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component1")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key")),
            ggl_obj_buf(GGL_STR("subkey"))
        ),
        ggl_obj_buf(
            GGL_STR("Ignored value- this argument would ideally be optional")
        ),
        GGL_ERR_NOENTRY // expect NOENTRY failure because
                        // `component/foo/bar/key/subkey` should not have exist
                        // or have been set after the previous insert failed
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component1")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value1")
        ), // `component/foo/bar/key` should still be
           // value1 after the previous insert failed
        GGL_ERR_OK
    );

    // Test to ensure a key which is a parent can't become a value as well
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component2")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("subkey"),
                              ggl_obj_buf(GGL_STR("value1")) })),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component2")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key")),
            ggl_obj_buf(GGL_STR("subkey"))
        ),
        ggl_obj_buf(GGL_STR("value1")),
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component2")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        -1,
        GGL_ERR_FAILURE
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component2")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("subkey"),
                              ggl_obj_buf(GGL_STR("value1")) })),
        GGL_ERR_OK
    );

    // Test to ensure you can't subscribe to a key which doesn't exist
    test_subscribe(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component3")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_ERR_NOENTRY
    );

    // Test to ensure subscribers and notifications work
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component3")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"),
                              ggl_obj_buf(GGL_STR("big value")) })),
        -1,
        GGL_ERR_OK
    );
    test_subscribe(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component3")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_ERR_OK
    );
    // TODO: Add in automated verification of the subscription callback in
    // response to these inserts. For now, check the logs manually (you should
    // see `I[subscription callback] (..): read component3/foo/bar/key`)
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component3")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"),
                              ggl_obj_buf(GGL_STR("the biggest value")) })),
        -1,
        GGL_ERR_OK
    );

    // Test to ensure you are notified for children and grandchildren key
    // updates
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component4")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        -1,
        GGL_ERR_OK
    );
    test_subscribe(GGL_LIST(ggl_obj_buf(GGL_STR("component4"))), GGL_ERR_OK);
    // Should see `I[subscription callback] (..): read component4/baz`)
    test_insert(
        GGL_LIST(ggl_obj_buf(GGL_STR("component4"))),
        ggl_obj_map(GGL_MAP({ GGL_STR("baz"), ggl_obj_buf(GGL_STR("value2")) })
        ),
        -1,
        GGL_ERR_OK
    );
    // Should see `I[subscription callback] (..): read component4/foo/bar/baz`)
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component4")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("baz"), ggl_obj_buf(GGL_STR("value3")) })
        ),
        -1,
        GGL_ERR_OK
    );

    // Test to ensure writes with older timestamps than the existing value are
    // ignored
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component6")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        1720000000001,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component6")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value2")) })
        ),
        1720000000000,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component6")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value1")),
        GGL_ERR_OK
    );

    // Test to ensure writes with identical timestamps overwrite the existing
    // value
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component7")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        1720000000001,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component7")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value2")) })
        ),
        1720000000001,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component7")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value2")),
        GGL_ERR_OK
    );

    // Test to ensure writes with newer timestamps overwrite the existing value
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component8")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        1720000000001,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component8")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), ggl_obj_buf(GGL_STR("value2")) })
        ),
        1720000000002,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component8")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value2")),
        GGL_ERR_OK
    );

    // Test to ensure some values in an object can be merged while others are
    // ignored due to timestamps
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component9")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key1"), ggl_obj_buf(GGL_STR("value1")) })
        ),
        1720000000000,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component9")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key2"), ggl_obj_buf(GGL_STR("value2")) })
        ),
        1720000000002,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component9")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_map(GGL_MAP(
            { GGL_STR("key1"), ggl_obj_buf(GGL_STR("value3")) },
            { GGL_STR("key2"), ggl_obj_buf(GGL_STR("value4")) }
        )),
        1720000000001,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component9")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key1"))
        ),
        ggl_obj_buf(GGL_STR("value3")),
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component9")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar")),
            ggl_obj_buf(GGL_STR("key2"))
        ),
        ggl_obj_buf(GGL_STR("value2")),
        GGL_ERR_OK
    );

    // Test to ensure null types can be stored and retrieved
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component10")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), GGL_OBJ_NULL })),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component10")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_OBJ_NULL,
        GGL_ERR_OK
    );

    // Test to write a buffer type directly
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component11")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_buf(GGL_STR("buffer")),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component11")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_buf(GGL_STR("buffer")),
        GGL_ERR_OK
    );

    // Test to write a null type directly
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component12")), ggl_obj_buf(GGL_STR("foo"))
        ),
        GGL_OBJ_NULL,
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component12")), ggl_obj_buf(GGL_STR("foo"))
        ),
        GGL_OBJ_NULL,
        GGL_ERR_OK
    );

    // Test to ensure a key can be deleted, not affecting its parent
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component13")), ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value")),
        -1,
        GGL_ERR_OK
    );
    test_delete(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component13")), ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component13")), ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_OBJ_NULL,
        GGL_ERR_NOENTRY
    );
    test_get(
        GGL_LIST(ggl_obj_buf(GGL_STR("component13"))),
        ggl_obj_map(GGL_MAP()),
        GGL_ERR_OK
    );

    // Test to ensure deletes are recursive
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component14")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        ggl_obj_buf(GGL_STR("value")),
        -1,
        GGL_ERR_OK
    );
    test_delete(GGL_LIST(ggl_obj_buf(GGL_STR("component14"))), GGL_ERR_OK);
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component14")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("bar"))
        ),
        GGL_OBJ_NULL,
        GGL_ERR_NOENTRY
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component14")), ggl_obj_buf(GGL_STR("foo"))
        ),
        GGL_OBJ_NULL,
        GGL_ERR_NOENTRY
    );
    test_get(
        GGL_LIST(ggl_obj_buf(GGL_STR("component14"))),
        GGL_OBJ_NULL,
        GGL_ERR_NOENTRY
    );

    // Test to ensure an empty map can be written and read
    test_insert(
        GGL_LIST(ggl_obj_buf(GGL_STR("component15"))),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(ggl_obj_buf(GGL_STR("component15"))),
        ggl_obj_map(GGL_MAP()),
        GGL_ERR_OK
    );

    // Test to ensure an empty map can be merged into an existing empty map
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component16")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component16")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component16")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP()),
        GGL_ERR_OK
    );

    // Test to ensure an empty map can be merged into an existing populated map
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component17")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), GGL_OBJ_NULL })),
        -1,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component17")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_OK
    );
    test_get(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component17")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), GGL_OBJ_NULL })),
        GGL_ERR_OK
    );

    // Test to ensure an empty map can not be merged into an existing value
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component18")), ggl_obj_buf(GGL_STR("foo"))
        ),
        ggl_obj_map(GGL_MAP({ GGL_STR("key"), GGL_OBJ_NULL })),
        -1,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component18")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_FAILURE
    );

    // Test to ensure an value can not be merged into an existing empty map
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component19")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component19")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_OBJ_NULL,
        -1,
        GGL_ERR_FAILURE
    );

    // Test to check subscriber behavior on deleted keys
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component20")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value1")),
        -1,
        GGL_ERR_OK
    );
    test_subscribe(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component20")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_ERR_OK
    );
    test_subscribe(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component20")), ggl_obj_buf(GGL_STR("foo"))
        ),
        GGL_ERR_OK
    );
    test_delete(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component20")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component20")),
            ggl_obj_buf(GGL_STR("foo")),
            ggl_obj_buf(GGL_STR("key"))
        ),
        ggl_obj_buf(GGL_STR("value2")),
        -1,
        GGL_ERR_OK
    ); // Should see one `read component20/foo/key` on the callback handle
       // created for component20/foo
    // Currently, the other subscription callback for component20/foo/key is not
    // notified. In the future, it would be good to have that behavior too. See
    // the docs/design/ggconfigd.md section "Subscription behavior for keys
    // which become deleted" for more info.

    // Test to ensure list reads all children, but not nested keys
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component21")), ggl_obj_buf(GGL_STR("key1"))
        ),
        ggl_obj_buf(GGL_STR("value1")),
        -1,
        GGL_ERR_OK
    );
    test_insert(
        GGL_LIST(
            ggl_obj_buf(GGL_STR("component21")), ggl_obj_buf(GGL_STR("key2"))
        ),
        ggl_obj_map(GGL_MAP(
            { GGL_STR("nested_key1"), ggl_obj_buf(GGL_STR("value2")) },
            { GGL_STR("nested_key2"), ggl_obj_buf(GGL_STR("value3")) }
        )),
        -1,
        GGL_ERR_OK
    );
    test_list(
        GGL_LIST(ggl_obj_buf(GGL_STR("component21"))),
        ggl_obj_list(
            GGL_LIST(ggl_obj_buf(GGL_STR("key1")), ggl_obj_buf(GGL_STR("key2")))
        ),
        GGL_ERR_OK
    );

    // Test to ensure list returns no entry if the key doesn't exist
    test_list(
        GGL_LIST(ggl_obj_buf(GGL_STR("non-existent")), ),
        GGL_OBJ_NULL,
        GGL_ERR_NOENTRY
    );

    // Test to ensure list returns invalid if the key is a value
    test_insert(
        GGL_LIST(ggl_obj_buf(GGL_STR("component22"))),
        ggl_obj_buf(GGL_STR("value")),
        -1,
        GGL_ERR_OK
    );
    test_list(
        GGL_LIST(ggl_obj_buf(GGL_STR("component22"))),
        GGL_OBJ_NULL,
        GGL_ERR_INVALID
    );

    // Test to ensure list returns an empty list if the key is an empty map
    test_insert(
        GGL_LIST(ggl_obj_buf(GGL_STR("component23"))),
        ggl_obj_map(GGL_MAP()),
        -1,
        GGL_ERR_OK
    );
    test_list(
        GGL_LIST(ggl_obj_buf(GGL_STR("component23"))),
        ggl_obj_list(GGL_LIST()),
        GGL_ERR_OK
    );

    // test_insert(
    //     GGL_LIST(ggl_obj_buf(GGL_STR("component")),
    //     ggl_obj_buf(GGL_STR("bar"))), ggl_obj_map(GGL_MAP({ GGL_STR("foo"),
    //     ggl_obj_buf(GGL_STR("value2")) }))
    // );
    // test_insert(
    //     GGL_LIST(ggl_obj_buf(GGL_STR("component")),
    //     ggl_obj_buf(GGL_STR("foo"))), ggl_obj_map(GGL_MAP({ GGL_STR("baz"),
    //     ggl_obj_buf(GGL_STR("value")) }))
    // );

    // test_insert(
    //     GGL_STR("global"),
    //     GGL_LIST(ggl_obj_buf(GGL_STR("global"))),
    //     ggl_obj_buf(GGL_STR("value"))  //TODO: Should something like this be
    //     possible?
    // );

    // TODO: verify If you have a subscriber on /foo and write
    // /foo/bar/baz = {"alpha":"data","bravo":"data","charlie":"data"}
    // , it should only signal the notification once.
    // This behavior needs to be implemented first.

    return 0;
}
