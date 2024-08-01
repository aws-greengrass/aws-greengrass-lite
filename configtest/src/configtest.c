#include <assert.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <openssl/ssl.h>
#include <stddef.h>
#include <stdint.h>

static void test_insert(
    GglBuffer component, GglBuffer test_key, GglBuffer test_value
) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("component"), GGL_OBJ(component) },
        { GGL_STR("key"), GGL_OBJ(test_key) },
        { GGL_STR("value"), GGL_OBJ(test_value) }
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

static void test_get(GglBuffer component, GglBuffer test_key) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");
    static uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("component"), GGL_OBJ(component) },
        { GGL_STR("key"), GGL_OBJ(test_key) },
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
    GGL_LOGI("configtest", "Subscription callback called for handle %d.", handle);
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

static void test_subscribe(GglBuffer component, GglBuffer key) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    GglMap params = GGL_MAP(
        { GGL_STR("component"), GGL_OBJ(component) },
        { GGL_STR("key"), GGL_OBJ(key) },
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
        exit(1);
    } else {
        GGL_LOGI(
            "test_subscribe",
            "Success %.*s : %d",
            (int) key.len,
            (char *) key.data,
            handle
        );
    }
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    test_insert(
        GGL_STR("component"), GGL_STR("foo/bar"), GGL_STR("another big value")
    );
    test_subscribe(GGL_STR("component"), GGL_STR("foo/bar"));
    test_insert(GGL_STR("component"), GGL_STR("foo/bar"), GGL_STR("big value"));
    test_insert(
        GGL_STR("component"), GGL_STR("foo/bar"), GGL_STR("the biggest value")
    );
    test_insert(GGL_STR("component"), GGL_STR("bar/foo"), GGL_STR("value2"));
    test_insert(GGL_STR("component"), GGL_STR("foo/baz"), GGL_STR("value"));
    test_insert(GGL_STR("global"), GGL_STR("global"), GGL_STR("value"));

    test_get(GGL_STR("component"), GGL_STR("foo/bar"));

    return 0;
}
