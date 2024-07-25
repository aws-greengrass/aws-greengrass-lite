#include "ggl/bump_alloc.h"
#include "ggl/client.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <assert.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

static void test_insert(
    GglBuffer component, GglBuffer test_key, GglBuffer test_value
) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");

    uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("component"),
          .val = { .type = GGL_TYPE_BUF, .buf = component } },
        { GGL_STR("key"), .val = { .type = GGL_TYPE_BUF, .buf = test_key } },
        { GGL_STR("value"), .val = { .type = GGL_TYPE_BUF, .buf = test_value } }
    );
    GglObject result;

    GglError error = ggl_call(
        server, GGL_STR("rpc_write"), params, &the_allocator.alloc, &result
    );

    if (error != GGL_ERR_OK) {
        GGL_LOGE("ggconfig test", "insert failure");
        assert(0);
    }
}

static void test_get(GglBuffer component, GglBuffer test_key) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");
    uint8_t big_buffer_for_bump[4096];
    GglBumpAlloc the_allocator
        = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    GglMap params = GGL_MAP(
        { GGL_STR("component"),
          .val = { .type = GGL_TYPE_BUF, .buf = component } },
        { GGL_STR("key"), .val = { .type = GGL_TYPE_BUF, .buf = test_key } },
    );
    GglObject result;

    GglError error = ggl_call(
        server, GGL_STR("rpc_read"), params, &the_allocator.alloc, &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("test_get", "error %d", error);
    }
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    test_insert(
        GGL_STR("component"), GGL_STR("foo/bar"), GGL_STR("another big value")
    );
    test_insert(GGL_STR("component"), GGL_STR("bar/foo"), GGL_STR("value2"));
    test_insert(GGL_STR("component"), GGL_STR("foo/baz"), GGL_STR("value"));
    test_insert(GGL_STR("global"), GGL_STR("global"), GGL_STR("value"));

    test_get(GGL_STR("component"), GGL_STR("foo/bar"));

    return 0;
}
