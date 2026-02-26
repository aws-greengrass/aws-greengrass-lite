#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/json_decode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <ggl/nucleus/init.h>
#include <inttypes.h>
#include <string.h>
#include <stdbool.h>

static char *print_key_path(GgList key_path) {
    static char path_string[64] = { 0 };
    memset(path_string, 0, sizeof(path_string));
    for (size_t x = 0; x < key_path.len; x++) {
        if (x > 0) {
            strncat(path_string, "/ ", 1);
        }
        strncat(
            path_string,
            (char *) gg_obj_into_buf(key_path.items[x]).data,
            gg_obj_into_buf(key_path.items[x]).len
        );
    }
    return path_string;
}

// a timestamp of -1 means no timestamp will be sent
static void test_insert(
    GgList test_key,
    GgObject test_value,
    int64_t timestamp,
    GgError expected_result
) {
    GG_LOGD(
        "test_insert: key=%s, timestamp=%d, expected_result=%s",
        print_key_path(test_key),
        (int) timestamp,
        gg_strerror(expected_result)
    );
    GgBuffer server = GG_STR("gg_config");

    static uint8_t alloc_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(alloc_mem));

    GgMap params = GG_MAP(
        gg_kv(GG_STR("key_path"), gg_obj_list(test_key)),
        gg_kv(GG_STR("value"), test_value),
        gg_kv(GG_STR("timestamp"), gg_obj_i64(timestamp))
    );
    if (timestamp < 0) {
        params.len -= 1;
    }

    // NOLINTNEXTLINE(clang-analyzer-optin.cplusplus.VirtualCall)
    GgObject result;

    GgError remote_error = GG_ERR_OK;
    GgError error = ggl_call(
        server, GG_STR("write"), params, &remote_error, &alloc, &result
    );

    if (expected_result != GG_ERR_OK && error != GG_ERR_REMOTE) {
        GG_LOGE(
            "insert of key %s expected error %s but there was not a remote error",
            print_key_path(test_key),
            gg_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GG_ERR_OK && error != GG_ERR_OK) {
        GG_LOGE(
            "insert of key %s did not expect error but got error %s and remote error %s",
            print_key_path(test_key),
            gg_strerror(error),
            gg_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GG_LOGE(
            "insert of key %s expected remote error %s but got %s",
            print_key_path(test_key),
            gg_strerror(expected_result),
            gg_strerror(remote_error)
        );
        assert(0);
    }
}

static void compare_objects(GgObject expected, GgObject result);

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_lists(GgList expected, GgList result) {
    if (result.len != expected.len) {
        GG_LOGE(
            "expected list of length %d got %d",
            (int) expected.len,
            (int) result.len
        );
        return;
    }
    for (size_t i = 0; i < expected.len; i++) {
        GgObject expected_item = expected.items[i];
        GgObject result_item = result.items[i];
        compare_objects(expected_item, result_item);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_maps(GgMap expected, GgMap result) {
    if (result.len != expected.len) {
        GG_LOGE(
            "expected map of length %d got %d",
            (int) expected.len,
            (int) result.len
        );
        return;
    }
    GG_MAP_FOREACH (expected_pair, expected) {
        GgBuffer expected_key = gg_kv_key(*expected_pair);
        GgObject expected_val = *gg_kv_val(expected_pair);
        bool found = false;
        GG_MAP_FOREACH (result_pair, result) {
            if (gg_buffer_eq(expected_key, gg_kv_key(*result_pair))) {
                found = true;
                compare_objects(expected_val, *gg_kv_val(result_pair));
                break;
            }
        }
        GG_MAP_FOREACH (result_pair, result) {
            if (gg_buffer_eq(expected_key, gg_kv_key(*result_pair))) {
                found = true;
                compare_objects(expected_val, *gg_kv_val(result_pair));
                break;
            }
        }
        if (!found) {
            GG_LOGE(
                "expected key %.*s not found",
                (int) expected_key.len,
                (char *) expected_key.data
            );
        }
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_objects(GgObject expected, GgObject result) {
    switch (gg_obj_type(expected)) {
    case GG_TYPE_BOOLEAN:
        if (gg_obj_type(result) != GG_TYPE_BOOLEAN) {
            GG_LOGE("expected boolean, got %d", gg_obj_type(result));
            return;
        }
        if (gg_obj_into_bool(result) != gg_obj_into_bool(expected)) {
            GG_LOGE(
                "expected %d got %d",
                gg_obj_into_bool(expected),
                gg_obj_into_bool(result)
            );
        }
        break;
    case GG_TYPE_I64:
        if (gg_obj_type(result) != GG_TYPE_I64) {
            GG_LOGE("expected i64, got %d", gg_obj_type(result));
            return;
        }
        if (gg_obj_into_i64(result) != gg_obj_into_i64(expected)) {
            GG_LOGE(
                "expected %" PRId64 " got %" PRId64,
                gg_obj_into_i64(expected),
                gg_obj_into_i64(result)
            );
        }
        break;
    case GG_TYPE_F64:
        if (gg_obj_type(result) != GG_TYPE_F64) {
            GG_LOGE("expected f64, got %d", gg_obj_type(result));
            return;
        }
        if (gg_obj_into_f64(result) != gg_obj_into_f64(expected)) {
            GG_LOGE(
                "expected %f got %f",
                gg_obj_into_f64(expected),
                gg_obj_into_f64(result)
            );
        }
        break;
    case GG_TYPE_BUF:
        if (gg_obj_type(result) != GG_TYPE_BUF) {
            GG_LOGE("expected buffer, got %d", gg_obj_type(result));
            return;
        }
        if (strncmp(
                (const char *) gg_obj_into_buf(result).data,
                (const char *) gg_obj_into_buf(expected).data,
                gg_obj_into_buf(result).len
            )
            != 0) {
            GG_LOGE(
                "expected %.*s got %.*s",
                (int) gg_obj_into_buf(expected).len,
                (char *) gg_obj_into_buf(expected).data,
                (int) gg_obj_into_buf(result).len,
                (char *) gg_obj_into_buf(result).data
            );
            return;
        }
        break;
    case GG_TYPE_LIST:
        if (gg_obj_type(result) != GG_TYPE_LIST) {
            GG_LOGE("expected list, got %d", gg_obj_type(result));
            return;
        }
        compare_lists(gg_obj_into_list(expected), gg_obj_into_list(result));
        break;
    case GG_TYPE_MAP:
        if (gg_obj_type(result) != GG_TYPE_MAP) {
            GG_LOGE("expected map, got %d", gg_obj_type(result));
            return;
        }
        compare_maps(gg_obj_into_map(expected), gg_obj_into_map(result));
        break;
    case GG_TYPE_NULL:
        if (gg_obj_type(result) != GG_TYPE_NULL) {
            GG_LOGE("expected null, got %d", gg_obj_type(result));
            return;
        }
        break;
    default:
        GG_LOGE("unexpected type %d", gg_obj_type(expected));
        break;
    }
}

static void test_get(
    GgList test_key_path, GgObject expected_object, GgError expected_result
) {
    GG_LOGD(
        "test_get %s, expecting %s",
        print_key_path(test_key_path),
        gg_strerror(expected_result)
    );
    GgBuffer server = GG_STR("gg_config");
    static uint8_t alloc_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(alloc_mem));

    GgMap params
        = GG_MAP(gg_kv(GG_STR("key_path"), gg_obj_list(test_key_path)));
    GgObject result;

    GgError remote_error = GG_ERR_OK;
    GgError error = ggl_call(
        server, GG_STR("read"), params, &remote_error, &alloc, &result
    );
    if (expected_result != GG_ERR_OK && error != GG_ERR_REMOTE) {
        GG_LOGE(
            "get key %s expected result %s but there was not a remote error",
            print_key_path(test_key_path),
            gg_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GG_ERR_OK && error != GG_ERR_OK) {
        GG_LOGE(
            "get key %s did not expect error but got error %s and remote error %s",
            print_key_path(test_key_path),
            gg_strerror(error),
            gg_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GG_LOGE(
            "get key %s expected result %s but got %s",
            print_key_path(test_key_path),
            gg_strerror(expected_result),
            gg_strerror(remote_error)
        );
        assert(0);
        return;
    }
    if (expected_result == GG_ERR_OK) {
        compare_objects(expected_object, result);
    }
}

static void test_list(
    GgList test_key_path, GgObject expected_object, GgError expected_result
) {
    GG_LOGD(
        "test_list %s, expecting %s",
        print_key_path(test_key_path),
        gg_strerror(expected_result)
    );
    GgBuffer server = GG_STR("gg_config");
    static uint8_t alloc_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(alloc_mem));

    GgMap params
        = GG_MAP(gg_kv(GG_STR("key_path"), gg_obj_list(test_key_path)));
    GgObject result;

    GgError remote_error = GG_ERR_OK;
    GgError error = ggl_call(
        server, GG_STR("list"), params, &remote_error, &alloc, &result
    );
    if (expected_result != GG_ERR_OK && error != GG_ERR_REMOTE) {
        GG_LOGE(
            "list key %s expected result %s but there was not a remote error",
            print_key_path(test_key_path),
            gg_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GG_ERR_OK && error != GG_ERR_OK) {
        GG_LOGE(
            "list key %s did not expect error but got error %s and remote error %s",
            print_key_path(test_key_path),
            gg_strerror(error),
            gg_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GG_LOGE(
            "list key %s expected result %s but got %s",
            print_key_path(test_key_path),
            gg_strerror(expected_result),
            gg_strerror(remote_error)
        );
        assert(0);
        return;
    }
    if (expected_result == GG_ERR_OK) {
        compare_objects(expected_object, result);
    }
}

static void test_delete(GgList key_path, GgError expected_result) {
    GG_LOGD(
        "test_delete %s, expecting %s",
        print_key_path(key_path),
        gg_strerror(expected_result)
    );
    GgBuffer server = GG_STR("gg_config");

    GgMap params = GG_MAP(gg_kv(GG_STR("key_path"), gg_obj_list(key_path)));

    GgError remote_error = GG_ERR_OK;
    GgError error
        = ggl_call(server, GG_STR("delete"), params, &remote_error, NULL, NULL);
    if (expected_result != GG_ERR_OK && error != GG_ERR_REMOTE) {
        GG_LOGE(
            "delete key %s expected result %s but there was not a remote error",
            print_key_path(key_path),
            gg_strerror(expected_result)
        );
        assert(0);
    }
    if (expected_result == GG_ERR_OK && error != GG_ERR_OK) {
        GG_LOGE(
            "delete key %s did not expect error but got error %s and remote error %s",
            print_key_path(key_path),
            gg_strerror(error),
            gg_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_result) {
        GG_LOGE(
            "delete key %s expected result %s but got %s",
            print_key_path(key_path),
            gg_strerror(expected_result),
            gg_strerror(remote_error)
        );
        assert(0);
        return;
    }
}

static GgError subscription_callback(
    void *ctx, unsigned int handle, GgObject data
) {
    (void) ctx;
    (void) data;
    GG_LOGI("Subscription callback called for handle %d.", handle);
    if (gg_obj_type(data) == GG_TYPE_LIST) {
        GG_LOGI("read %s", print_key_path(gg_obj_into_list(data)));
    } else {
        GG_LOGE("expected a list ");
    }
    return GG_ERR_OK;
}

static void subscription_close(void *ctx, unsigned int handle) {
    (void) ctx;
    (void) handle;
    GG_LOGI("called");
}

static void test_subscribe(GgList key, GgError expected_response) {
    GG_LOGD(
        "test_subscribe %s, expecting %s",
        print_key_path(key),
        gg_strerror(expected_response)
    );
    GgBuffer server = GG_STR("gg_config");

    GgMap params = GG_MAP(gg_kv(GG_STR("key_path"), gg_obj_list(key)));
    uint32_t handle = 0;
    GgError remote_error = GG_ERR_OK;
    GgError error = ggl_subscribe(
        server,
        GG_STR("subscribe"),
        params,
        subscription_callback,
        subscription_close,
        NULL,
        &remote_error,
        &handle
    );
    if (expected_response != GG_ERR_OK && error != GG_ERR_REMOTE) {
        GG_LOGE(
            "subscribe key %s expected result %d but there was not a remote error",
            print_key_path(key),
            (int) expected_response
        );
        assert(0);
    }
    if (expected_response == GG_ERR_OK && error != GG_ERR_OK) {
        GG_LOGE(
            "insert of key %s did not expect error but got error %s and remote error %s",
            print_key_path(key),
            gg_strerror(error),
            gg_strerror(remote_error)
        );
        assert(0);
    }
    if (remote_error != expected_response) {
        GG_LOGE(
            "subscribe key %s expected result %s but got %s",
            print_key_path(key),
            gg_strerror(expected_response),
            gg_strerror(error)
        );
        assert(0);
        return;
    }
    if (error == GG_ERR_OK) {
        GG_LOGI("Success! key: %s handle: %d", print_key_path(key), handle);
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
    char test_key_path_json[] = "[\"component\",\"foobar\"]";
    char test_value_json[]
        = "{\"foo\":{\"bar\":{\"baz\":[ 1,2,3,4],\"qux\":1},\"quux\": \"string\" },\"corge\" : true, \"grault\" : false}";
    GgObject test_key_path_object;
    GgObject test_value_object;
    static uint8_t big_buffer[4096];
    GG_LOGI("test begun");

    GgArena arena = gg_arena_init(GG_BUF(big_buffer));
    GgError ret = gg_json_decode_destructive(
        gg_buffer_from_null_term(test_key_path_json),
        &arena,
        &test_key_path_object
    );
    GG_LOGI("json decode complete %d", ret);

    // Needs error checking?
    (void) gg_json_decode_destructive(
        gg_buffer_from_null_term(test_value_json), &arena, &test_value_object
    );

    if (gg_obj_type(test_key_path_object) == GG_TYPE_LIST) {
        GG_LOGI("found a list in the json path");
    } else {
        GG_LOGE("json path is not a list");
    }

    GgMap params = GG_MAP(
        gg_kv(GG_STR("key_path"), test_key_path_object),
        gg_kv(GG_STR("value"), test_value_object)
    );
    ret = ggl_notify(GG_STR("gg_config"), GG_STR("write"), params);
    GG_LOGI("test complete %d", ret);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    ggl_nucleus_init();

    // Test to ensure getting a key which doesn't exist works
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component")), gg_obj_buf(GG_STR("nonexistent"))
        ),
        gg_obj_map((GgMap) { 0 }),
        GG_ERR_NOENTRY
    );

    // Test to ensure recursive/object write and read works
    test_write_object();
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component")),
            gg_obj_buf(GG_STR("foobar")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("qux"))
        ),
        gg_obj_i64(1),
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component")),
            gg_obj_buf(GG_STR("foobar")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("baz"))
        ),
        gg_obj_list(
            GG_LIST(gg_obj_i64(1), gg_obj_i64(2), gg_obj_i64(3), gg_obj_i64(4))
        ),
        GG_ERR_OK
    );

    GgObject bar = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("qux"), gg_obj_i64(1)),
        gg_kv(
            GG_STR("baz"),
            gg_obj_list(GG_LIST(
                gg_obj_i64(1), gg_obj_i64(2), gg_obj_i64(3), gg_obj_i64(4)
            ))
        )
    ));

    GgObject foo = gg_obj_map(GG_MAP(
        gg_kv(GG_STR("bar"), bar),
        gg_kv(GG_STR("quux"), gg_obj_buf(GG_STR("string")))
    ));

    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component")), gg_obj_buf(GG_STR("foobar")),
        ),
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("foo"), foo),
            gg_kv(GG_STR("corge"), gg_obj_bool(true)),
            gg_kv(GG_STR("grault"), gg_obj_bool(false)),
        )),
        GG_ERR_OK
    );

    // Test to ensure a key which is a value can't become a parent as well
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component1")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value1"))))),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component1")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value1")),
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component1")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("subkey"), gg_obj_buf(GG_STR("value2"))))
        ),
        -1,
        GG_ERR_FAILURE // expect failure because `component/foo/bar/key` is
                       // already a value, so it should not also be a parent of
                       // a subkey
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component1")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key")),
            gg_obj_buf(GG_STR("subkey"))
        ),
        gg_obj_buf(
            GG_STR("Ignored value- this argument would ideally be optional")
        ),
        GG_ERR_NOENTRY // expect NOENTRY failure because
                       // `component/foo/bar/key/subkey` should not have exist
                       // or have been set after the previous insert failed
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component1")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value1")), // `component/foo/bar/key` should still be
                                      // value1 after the previous insert failed
        GG_ERR_OK
    );

    // Test to ensure a key which is a parent can't become a value as well
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component2")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("subkey"), gg_obj_buf(GG_STR("value1"))))
        ),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component2")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key")),
            gg_obj_buf(GG_STR("subkey"))
        ),
        gg_obj_buf(GG_STR("value1")),
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component2")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value1"))))),
        -1,
        GG_ERR_FAILURE
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component2")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("subkey"), gg_obj_buf(GG_STR("value1"))))
        ),
        GG_ERR_OK
    );

    // Test to ensure you can't subscribe to a key which doesn't exist
    test_subscribe(
        GG_LIST(
            gg_obj_buf(GG_STR("component3")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        GG_ERR_NOENTRY
    );

    // Test to ensure subscribers and notifications work
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component3")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("big value"))))
        ),
        -1,
        GG_ERR_OK
    );
    test_subscribe(
        GG_LIST(
            gg_obj_buf(GG_STR("component3")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        GG_ERR_OK
    );
    // TODO: Add in automated verification of the subscription callback in
    // response to these inserts. For now, check the logs manually (you should
    // see `I[subscription callback] (..): read component3/foo/bar/key`)
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component3")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("the biggest value")))
        )),
        -1,
        GG_ERR_OK
    );

    // Test to ensure you are notified for children and grandchildren key
    // updates
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component4")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value1"))))),
        -1,
        GG_ERR_OK
    );
    test_subscribe(GG_LIST(gg_obj_buf(GG_STR("component4"))), GG_ERR_OK);
    // Should see `I[subscription callback] (..): read component4/baz`)
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component4"))),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("baz"), gg_obj_buf(GG_STR("value2"))))),
        -1,
        GG_ERR_OK
    );
    // Should see `I[subscription callback] (..): read component4/foo/bar/baz`)
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component4")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("baz"), gg_obj_buf(GG_STR("value3"))))),
        -1,
        GG_ERR_OK
    );

    // Test to ensure writes with older timestamps than the existing value are
    // ignored
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component6")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value1"))))),
        1720000000001,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component6")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value2"))))),
        1720000000000,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component6")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value1")),
        GG_ERR_OK
    );

    // Test to ensure writes with identical timestamps overwrite the existing
    // value
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component7")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value1"))))),
        1720000000001,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component7")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value2"))))),
        1720000000001,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component7")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value2")),
        GG_ERR_OK
    );

    // Test to ensure writes with newer timestamps overwrite the existing value
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component8")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value1"))))),
        1720000000001,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component8")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), gg_obj_buf(GG_STR("value2"))))),
        1720000000002,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component8")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value2")),
        GG_ERR_OK
    );

    // Test to ensure some values in an object can be merged while others are
    // ignored due to timestamps
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component9")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key1"), gg_obj_buf(GG_STR("value1"))))),
        1720000000000,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component9")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key2"), gg_obj_buf(GG_STR("value2"))))),
        1720000000002,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component9")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("key1"), gg_obj_buf(GG_STR("value3"))),
            gg_kv(GG_STR("key2"), gg_obj_buf(GG_STR("value4")))
        )),
        1720000000001,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component9")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key1"))
        ),
        gg_obj_buf(GG_STR("value3")),
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component9")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar")),
            gg_obj_buf(GG_STR("key2"))
        ),
        gg_obj_buf(GG_STR("value2")),
        GG_ERR_OK
    );

    // Test to ensure null types can be stored and retrieved
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component10")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), GG_OBJ_NULL))),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component10")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        GG_OBJ_NULL,
        GG_ERR_OK
    );

    // Test to write a buffer type directly
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component11")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_buf(GG_STR("buffer")),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component11")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_buf(GG_STR("buffer")),
        GG_ERR_OK
    );

    // Test to write a null type directly
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component12")), gg_obj_buf(GG_STR("foo"))),
        GG_OBJ_NULL,
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component12")), gg_obj_buf(GG_STR("foo"))),
        GG_OBJ_NULL,
        GG_ERR_OK
    );

    // Test to ensure a key can be deleted, not affecting its parent
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component13")), gg_obj_buf(GG_STR("key"))),
        gg_obj_buf(GG_STR("value")),
        -1,
        GG_ERR_OK
    );
    test_delete(
        GG_LIST(gg_obj_buf(GG_STR("component13")), gg_obj_buf(GG_STR("key"))),
        GG_ERR_OK
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component13")), gg_obj_buf(GG_STR("key"))),
        GG_OBJ_NULL,
        GG_ERR_NOENTRY
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component13"))),
        gg_obj_map(GG_MAP()),
        GG_ERR_OK
    );

    // Test to ensure deletes are recursive
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component14")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        gg_obj_buf(GG_STR("value")),
        -1,
        GG_ERR_OK
    );
    test_delete(GG_LIST(gg_obj_buf(GG_STR("component14"))), GG_ERR_OK);
    test_get(
        GG_LIST(
            gg_obj_buf(GG_STR("component14")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("bar"))
        ),
        GG_OBJ_NULL,
        GG_ERR_NOENTRY
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component14")), gg_obj_buf(GG_STR("foo"))),
        GG_OBJ_NULL,
        GG_ERR_NOENTRY
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component14"))), GG_OBJ_NULL, GG_ERR_NOENTRY
    );

    // Test to ensure an empty map can be written and read
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component15"))),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component15"))),
        gg_obj_map(GG_MAP()),
        GG_ERR_OK
    );

    // Test to ensure an empty map can be merged into an existing empty map
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component16")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component16")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component16")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP()),
        GG_ERR_OK
    );

    // Test to ensure an empty map can be merged into an existing populated map
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component17")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), GG_OBJ_NULL))),
        -1,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component17")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_OK
    );
    test_get(
        GG_LIST(gg_obj_buf(GG_STR("component17")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), GG_OBJ_NULL))),
        GG_ERR_OK
    );

    // Test to ensure an empty map can not be merged into an existing value
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component18")), gg_obj_buf(GG_STR("foo"))),
        gg_obj_map(GG_MAP(gg_kv(GG_STR("key"), GG_OBJ_NULL))),
        -1,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component18")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_FAILURE
    );

    // Test to ensure an value can not be merged into an existing empty map
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component19")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component19")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        GG_OBJ_NULL,
        -1,
        GG_ERR_FAILURE
    );

    // Test to check subscriber behavior on deleted keys
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component20")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value1")),
        -1,
        GG_ERR_OK
    );
    test_subscribe(
        GG_LIST(
            gg_obj_buf(GG_STR("component20")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        GG_ERR_OK
    );
    test_subscribe(
        GG_LIST(gg_obj_buf(GG_STR("component20")), gg_obj_buf(GG_STR("foo"))),
        GG_ERR_OK
    );
    test_delete(
        GG_LIST(
            gg_obj_buf(GG_STR("component20")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(
            gg_obj_buf(GG_STR("component20")),
            gg_obj_buf(GG_STR("foo")),
            gg_obj_buf(GG_STR("key"))
        ),
        gg_obj_buf(GG_STR("value2")),
        -1,
        GG_ERR_OK
    ); // Should see one `read component20/foo/key` on the callback handle
       // created for component20/foo
    // Currently, the other subscription callback for component20/foo/key is not
    // notified. In the future, it would be good to have that behavior too. See
    // the docs/design/ggconfigd.md section "Subscription behavior for keys
    // which become deleted" for more info.

    // Test to ensure list reads all children, but not nested keys
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component21")), gg_obj_buf(GG_STR("key1"))),
        gg_obj_buf(GG_STR("value1")),
        -1,
        GG_ERR_OK
    );
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component21")), gg_obj_buf(GG_STR("key2"))),
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("nested_key1"), gg_obj_buf(GG_STR("value2"))),
            gg_kv(GG_STR("nested_key2"), gg_obj_buf(GG_STR("value3")))
        )),
        -1,
        GG_ERR_OK
    );
    test_list(
        GG_LIST(gg_obj_buf(GG_STR("component21"))),
        gg_obj_list(
            GG_LIST(gg_obj_buf(GG_STR("key1")), gg_obj_buf(GG_STR("key2")))
        ),
        GG_ERR_OK
    );

    // Test to ensure list returns no entry if the key doesn't exist
    test_list(
        GG_LIST(gg_obj_buf(GG_STR("non-existent")), ),
        GG_OBJ_NULL,
        GG_ERR_NOENTRY
    );

    // Test to ensure list returns invalid if the key is a value
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component22"))),
        gg_obj_buf(GG_STR("value")),
        -1,
        GG_ERR_OK
    );
    test_list(
        GG_LIST(gg_obj_buf(GG_STR("component22"))), GG_OBJ_NULL, GG_ERR_INVALID
    );

    // Test to ensure list returns an empty list if the key is an empty map
    test_insert(
        GG_LIST(gg_obj_buf(GG_STR("component23"))),
        gg_obj_map(GG_MAP()),
        -1,
        GG_ERR_OK
    );
    test_list(
        GG_LIST(gg_obj_buf(GG_STR("component23"))),
        gg_obj_list(GG_LIST()),
        GG_ERR_OK
    );

    // test_insert(
    //     GG_LIST(gg_obj_buf(GG_STR("component")),
    //     gg_obj_buf(GG_STR("bar"))), gg_obj_map(GG_MAP({ GG_STR("foo"),
    //     gg_obj_buf(GG_STR("value2")) }))
    // );
    // test_insert(
    //     GG_LIST(gg_obj_buf(GG_STR("component")),
    //     gg_obj_buf(GG_STR("foo"))), gg_obj_map(GG_MAP({ GG_STR("baz"),
    //     gg_obj_buf(GG_STR("value")) }))
    // );

    // test_insert(
    //     GG_STR("global"),
    //     GG_LIST(gg_obj_buf(GG_STR("global"))),
    //     gg_obj_buf(GG_STR("value"))  //TODO: Should something like this be
    //     possible?
    // );

    // TODO: verify If you have a subscriber on /foo and write
    // /foo/bar/baz = {"alpha":"data","bravo":"data","charlie":"data"}
    // , it should only signal the notification once.
    // This behavior needs to be implemented first.

    return 0;
}
