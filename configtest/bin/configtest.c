#include <assert.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/object.h>
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

static void test_insert(
    GglBuffer component, GglList test_key, GglObject test_value
) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("componentName"), GGL_OBJ(component) },
        { GGL_STR("keyPath"), GGL_OBJ(test_key) },
        { GGL_STR("valueToMerge"), test_value },
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

static void test_get(GglBuffer component, GglList test_key_path) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");
    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("componentName"), GGL_OBJ(component) },
        { GGL_STR("keyPath"), GGL_OBJ(test_key_path) },
    );
    GglObject result;

    GglError error = ggl_call(
        server, GGL_STR("read"), params, NULL, &the_allocator.alloc, &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("test_get", "error %d", error);
    } else {
        if (result.type == GGL_TYPE_BUF) {
            GGL_LOGI(
                "test_get",
                "read %.*s",
                (int) result.buf.len,
                (char *) result.buf.data
            );
        }
    }
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

static void test_subscribe(GglBuffer component, GglList key) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    GglMap params = GGL_MAP(
        { GGL_STR("componentName"), GGL_OBJ(component) },
        { GGL_STR("keyPath"), GGL_OBJ(key) },
    );
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
keyPath = ["foobar"]
valueToMerge = {
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
    char json_path_string[] = "[\"foobar\"]";
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
        { GGL_STR("componentName"), GGL_OBJ(GGL_STR("component")) },
        { GGL_STR("keyPath"), test_key_path_object },
        { GGL_STR("valueToMerge"), test_value_object },
        { GGL_STR("timeStamp"), GGL_OBJ_I64(1723142212) }
    );
    error = ggl_notify(GGL_STR("/aws/ggl/ggconfigd"), GGL_STR("write"), params);
    GGL_LOGI("test_write_object", "test complete %d", error);
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    test_write_object();

    test_insert(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar")),
        GGL_OBJ_MAP({ GGL_STR("key"), GGL_OBJ_STR("value") })
    );
    test_get(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar"), GGL_OBJ_STR("key"))
    );

    test_subscribe(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar"), GGL_OBJ_STR("key"))
    );
    test_insert(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar")),
        GGL_OBJ_MAP({ GGL_STR("key"), GGL_OBJ_STR("big value") })
    );
    test_insert(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("foo"), GGL_OBJ_STR("bar")),
        GGL_OBJ_MAP({ GGL_STR("key"), GGL_OBJ_STR("the biggest value") })
    );
    test_insert(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("bar")),
        GGL_OBJ_MAP({ GGL_STR("foo"), GGL_OBJ_STR("value2") })
    );
    test_insert(
        GGL_STR("component"),
        GGL_LIST(GGL_OBJ_STR("foo")),
        GGL_OBJ_MAP({ GGL_STR("baz"), GGL_OBJ_STR("value") })
    );
    // test_insert(
    //     GGL_STR("global"),
    //     GGL_LIST(GGL_OBJ_STR("global")),
    //     GGL_OBJ_STR("value")  //TODO: Should something like this be possible?
    // );

    // TODO: verify If you have a subscriber on /foo and write
    // /foo/bar/baz = {"alpha":"data","bravo":"data","charlie":"data"}
    // , it should only signal the notification once.

    // TODO: if a notified process writes to /foo/<someplace> we can trigger an infinite update loop?

    return 0;
}
