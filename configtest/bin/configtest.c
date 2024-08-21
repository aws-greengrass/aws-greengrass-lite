#include <assert.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

static char *print_key_path(GglList *key_path) {
    static char path_string[64] = { 0 };
    memset(path_string, 0, sizeof(path_string));
    for (size_t x = 0; x < key_path->len; x++) {
        if (x > 0) {
            strncat(path_string, "/ ", 1);
        }
        strncat(
            path_string,
            (char *) key_path->items[x].buf.data,
            key_path->items[x].buf.len
        );
    }
    return path_string;
}

static void test_insert(GglList test_key, GglObject test_value) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"), GGL_OBJ(test_key) },
        { GGL_STR("value"), test_value },
        { GGL_STR("timeStamp"), GGL_OBJ_I64(1723142212) }
    );
    GglObject result;

    GglError error = ggl_call(
        server, GGL_STR("write"), params, NULL, &the_allocator.alloc, &result
    );

    if (error != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
        assert(0);
    }
}

// NOLINTNEXTLINE(misc-no-recursion)
static void compare_objects(GglObject expected, GglObject result) {
    switch (expected.type) {
    case GGL_TYPE_BOOLEAN:
        if (result.type != GGL_TYPE_BOOLEAN) {
            GGL_LOGE("test_get", "expected boolean, got %d", result.type);
            return;
        }
        if (result.boolean != expected.boolean) {
            GGL_LOGE(
                "test_get",
                "expected %d got %d",
                expected.boolean,
                result.boolean
            );
        }
        break;
    case GGL_TYPE_I64:
        if (result.type != GGL_TYPE_I64) {
            GGL_LOGE("test_get", "expected i64, got %d", result.type);
            return;
        }
        if (result.i64 != expected.i64) {
            GGL_LOGE(
                "test_get", "expected %ld got %ld", expected.i64, result.i64
            );
        }
        break;
    case GGL_TYPE_F64:
        if (result.type != GGL_TYPE_F64) {
            GGL_LOGE("test_get", "expected f64, got %d", result.type);
            return;
        }
        if (result.f64 != expected.f64) {
            GGL_LOGE(
                "test_get", "expected %f got %f", expected.f64, result.f64
            );
        }
        break;
    case GGL_TYPE_BUF:
        if (result.type != GGL_TYPE_BUF) {
            GGL_LOGE("test_get", "expected buffer, got %d", result.type);
            return;
        }
        if (strncmp(
                (const char *) result.buf.data,
                (const char *) expected.buf.data,
                result.buf.len
            )
            != 0) {
            GGL_LOGE(
                "test_get",
                "expected %.*s got %.*s",
                (int) expected.buf.len,
                (char *) expected.buf.data,
                (int) result.buf.len,
                (char *) result.buf.data
            );
            return;
        }
        break;
    case GGL_TYPE_LIST:
        if (result.type != GGL_TYPE_LIST) {
            GGL_LOGE("test_get", "expected list, got %d", result.type);
            return;
        }
        if (result.list.len != expected.list.len) {
            GGL_LOGE(
                "test_get",
                "expected list of length %d got %d",
                (int) expected.list.len,
                (int) result.list.len
            );
            return;
        }
        for (size_t i = 0; i < expected.list.len; i++) {
            GglObject expected_item = expected.list.items[i];
            GglObject result_item = result.list.items[i];
            compare_objects(expected_item, result_item);
        }
        break;
    case GGL_TYPE_MAP:
        if (result.type != GGL_TYPE_MAP) {
            GGL_LOGE("test_get", "expected map, got %d", result.type);
            return;
        }
        if (result.map.len != expected.map.len) {
            GGL_LOGE(
                "test_get",
                "expected map of length %d got %d",
                (int) expected.map.len,
                (int) result.map.len
            );
            return;
        }
        for (size_t i = 0; i < expected.map.len; i++) {
            GglBuffer expected_key = expected.map.pairs[i].key;
            GglObject expected_val = expected.map.pairs[i].val;
            bool found = false;
            for (size_t j = 0; j < result.map.len; j++) {
                if (strncmp(
                        (const char *) expected_key.data,
                        (const char *) result.map.pairs[j].key.data,
                        expected_key.len
                    )
                    == 0) {
                    found = true;
                    GglObject result_item = result.map.pairs[j].val;
                    compare_objects(expected_val, result_item);
                    break;
                }
            }
            if (!found) {
                GGL_LOGE(
                    "test_get",
                    "expected key %.*s not found",
                    (int) expected_key.len,
                    (char *) expected_key.data
                );
            }
        }
        break;
    default:
        GGL_LOGE("test_get", "unexpected type %d", expected.type);
        break;
    }
}

static void test_get(GglList test_key_path, GglObject expected) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");
    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP({ GGL_STR("key_path"), GGL_OBJ(test_key_path) }, );
    GglObject result;

    GglError error = ggl_call(
        server, GGL_STR("read"), params, NULL, &the_allocator.alloc, &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("test_get", "error %d", error);
        return;
    }
    compare_objects(expected, result);
    return;
}

static GglError subscription_callback(
    void *ctx, unsigned int handle, GglObject data
) {
    (void) ctx;
    (void) data;
    GGL_LOGI(
        "configtest", "Subscription callback called for handle %d.", handle
    );
    if (data.type == GGL_TYPE_BUF) {
        GGL_LOGI(
            "subscription callback",
            "read %.*s",
            (int) data.buf.len,
            (char *) data.buf.data
        );
    } else {
        GGL_LOGE("subscription callback", "expected a buffer");
    }
    return GGL_ERR_OK;
}

static void subscription_close(void *ctx, unsigned int handle) {
    (void) ctx;
    (void) handle;
    GGL_LOGI("subscription close", "called");
}

static void test_subscribe(GglList key) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    GglMap params = GGL_MAP({ GGL_STR("key_path"), GGL_OBJ(key) }, );
    uint32_t handle;
    GglError error = ggl_subscribe(
        server,
        GGL_STR("subscribe"),
        params,
        subscription_callback,
        subscription_close,
        NULL,
        NULL, // TODO: this must be tested
        &handle
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("test_subscribe", "error %d", error);
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(1);
    } else {
        GGL_LOGI(
            "test_subscribe", "Success %s : %d", print_key_path(&key), handle
        );
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
    char json_path_string[] = "[\"component\",\"foobar\"]";
    char json_value_string[]
        = "{\"foo\":{\"bar\":{\"baz\":[ 1,2,3,4],\"qux\":1},\"quux\""
          ": \"string\" },\"corge\" : true, \"grault\" : false}";
    GglBuffer test_key_path_json = GGL_STR(json_path_string);
    GglBuffer test_value_json = GGL_STR(json_value_string);
    GglObject test_key_path_object;
    GglObject test_value_object;
    static uint8_t big_buffer[4096];
    GGL_LOGI("test_write_object", "test begun");

    GglBumpAlloc the_allocator = ggl_bump_alloc_init(GGL_BUF(big_buffer));
    GglError error = ggl_json_decode_destructive(
        test_key_path_json, &the_allocator.alloc, &test_key_path_object
    );
    GGL_LOGI("test_write_object", "json decode complete %d", error);

    ggl_json_decode_destructive(
        test_value_json, &the_allocator.alloc, &test_value_object
    );

    if (test_key_path_object.type == GGL_TYPE_LIST) {
        GGL_LOGI("test_write_object", "found a list in the json path");
    } else {
        GGL_LOGE("test_write_object", "json path is not a list");
    }

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"), test_key_path_object },
        { GGL_STR("value"), test_value_object },
        { GGL_STR("timeStamp"), GGL_OBJ_I64(1723142212) }
    );
    error = ggl_notify(GGL_STR("/aws/ggl/ggconfigd"), GGL_STR("write"), params);
    GGL_LOGI("test_write_object", "test complete %d", error);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    test_write_object();

    test_get(
        GGL_LIST(
            GGL_OBJ_STR("component"),
            GGL_OBJ_STR("foobar"),
            GGL_OBJ_STR("foo"),
            GGL_OBJ_STR("bar"),
            GGL_OBJ_STR("qux")
        ),
        GGL_OBJ_I64(1)
    );

    test_get(
        GGL_LIST(
            GGL_OBJ_STR("component"),
            GGL_OBJ_STR("foobar"),
            GGL_OBJ_STR("foo"),
            GGL_OBJ_STR("bar"),
            GGL_OBJ_STR("baz")
        ),
        GGL_OBJ_LIST(
            GGL_OBJ_I64(1), GGL_OBJ_I64(2), GGL_OBJ_I64(3), GGL_OBJ_I64(4)
        )
    );

    test_get(
        GGL_LIST(GGL_OBJ_STR("component"), GGL_OBJ_STR("foobar"), ),
        GGL_OBJ_MAP(
            (GglKV) { .key = GGL_STR("foo"),
                      .val = GGL_OBJ_MAP(
                          (GglKV) { .key = GGL_STR("bar"),
                                    .val = GGL_OBJ_MAP(
                                        (GglKV) { .key = GGL_STR("qux"),
                                                  .val = GGL_OBJ_I64(1) },
                                        (GglKV) { .key = GGL_STR("baz"),
                                                  .val = GGL_OBJ_LIST(
                                                      GGL_OBJ_I64(1),
                                                      GGL_OBJ_I64(2),
                                                      GGL_OBJ_I64(3),
                                                      GGL_OBJ_I64(4)
                                                  ) }
                                    ) },
                          (GglKV) { .key = GGL_STR("quux"),
                                    .val = GGL_OBJ_STR("string") }
                      ) },
            (GglKV) { .key = GGL_STR("corge"), .val = GGL_OBJ_BOOL(true) },
            (GglKV) { .key = GGL_STR("grault"), .val = GGL_OBJ_BOOL(false) },
        )
    );

    test_insert(
        GGL_LIST(
            GGL_OBJ_STR("component"), GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar")
        ),
        GGL_OBJ_MAP({ GGL_STR("key"), GGL_OBJ_STR("value1") })
    );
    test_get(
        GGL_LIST(
            GGL_OBJ_STR("component"),
            GGL_OBJ_STR("foo"),
            GGL_OBJ_STR("bar"),
            GGL_OBJ_STR("key")
        ),
        GGL_OBJ_STR("value1")
    );
    // TODO: FIXME: We currently allow a key to be both a value (leaf) and a
    // parent node. This should not be allowed. e.g. add a
    // constraint/check/logic to make sure that never happens during write
    // test_insert( // This insert should fail after already setting
    // component/foo/bar/key = value1
    //     GGL_LIST(
    //         GGL_OBJ_STR("component"), GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar"),
    //         GGL_OBJ_STR("key")
    //     ),
    //     GGL_OBJ_MAP({ GGL_STR("subkey"), GGL_OBJ_STR("value2") })
    // );
    // test_get(GGL_LIST(
    //     GGL_OBJ_STR("component"),
    //     GGL_OBJ_STR("foo"),
    //     GGL_OBJ_STR("bar"),
    //     GGL_OBJ_STR("key"),
    //     GGL_OBJ_STR("subkey")
    // ));
    // test_get(GGL_LIST( // should return bar:{key:value1} in a map
    //     GGL_OBJ_STR("component"),
    //     GGL_OBJ_STR("foo")
    // ));

    // TODO: Fix subscriber tests + logic
    test_subscribe(GGL_LIST(
        GGL_OBJ_STR("component"),
        GGL_OBJ_STR("foo"),
        GGL_OBJ_STR("bar"),
        GGL_OBJ_STR("key")
    ));
    // test_insert(
    //     GGL_LIST(
    //         GGL_OBJ_STR("component"), GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar")
    //     ),
    //     GGL_OBJ_MAP({ GGL_STR("key"), GGL_OBJ_STR("big value") })
    // );
    // test_insert(
    //     GGL_LIST(
    //         GGL_OBJ_STR("component"), GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar")
    //     ),
    //     GGL_OBJ_MAP({ GGL_STR("key"), GGL_OBJ_STR("the biggest value") })
    // );
    // test_insert(
    //     GGL_LIST(GGL_OBJ_STR("component"), GGL_OBJ_STR("bar")),
    //     GGL_OBJ_MAP({ GGL_STR("foo"), GGL_OBJ_STR("value2") })
    // );
    // test_insert(
    //     GGL_LIST(GGL_OBJ_STR("component"), GGL_OBJ_STR("foo")),
    //     GGL_OBJ_MAP({ GGL_STR("baz"), GGL_OBJ_STR("value") })
    // );

    // test_insert(
    //     GGL_STR("global"),
    //     GGL_LIST(GGL_OBJ_STR("global")),
    //     GGL_OBJ_STR("value")  //TODO: Should something like this be possible?
    // );

    // TODO: verify If you have a subscriber on /foo and write
    // /foo/bar/baz = {"alpha":"data","bravo":"data","charlie":"data"}
    // , it should only signal the notification once.

    // TODO: if a notified process writes to /foo/<someplace> we can trigger an
    // infinite update loop?

    return 0;
}
