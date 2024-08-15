// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_client.h"
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#define KEY_PREFIX "component/"
#define KEY_SUFFIX "/version"
#define KEY_PREFIX_LEN (sizeof(KEY_PREFIX) - 1U)
#define KEY_SUFFIX_LEN (sizeof(KEY_SUFFIX) - 1U)

static pthread_mutex_t bump_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;

// Check a component's version field in ggconfigd for proof of existence
GglError verify_component_exists(GglBuffer component_name) {
    if ((component_name.data == NULL) || (component_name.len == 0)
        || (component_name.len > 128U)) {
        return GGL_ERR_RANGE;
    }

    int ret = pthread_mutex_lock(&bump_alloc_mutex);
    if (ret < 0) {
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(pthread_mutex_unlock, bump_alloc_mutex);

    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");
    static uint8_t bump_buffer[4096];
    GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(bump_buffer));

    GglBuffer key
        = { .data = GGL_ALLOCN(
                &alloc.alloc,
                uint8_t,
                component_name.len + KEY_PREFIX_LEN + KEY_SUFFIX_LEN + 1U
            ),
            .len = component_name.len + KEY_PREFIX_LEN + KEY_SUFFIX_LEN };

    if (key.data == NULL) {
        return GGL_ERR_NOMEM;
    }

    key.data[0] = '\0';
    strncat((char *) key.data, KEY_PREFIX, KEY_PREFIX_LEN + 1U);
    strncat(
        (char *) key.data,
        (const char *) component_name.data,
        component_name.len
    );
    strncat((char *) key.data, KEY_SUFFIX, KEY_SUFFIX_LEN + 1U);

    GglMap params = GGL_MAP(
        { GGL_STR("component"), GGL_OBJ_STR("gghealthd") },
        { GGL_STR("key"), GGL_OBJ(key) },
    );
    GglObject result;

    GglError method_error = GGL_ERR_OK;
    GglError error = ggl_call(
        server, GGL_STR("read"), params, &method_error, &alloc.alloc, &result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("gghealthd", "failed to connect to ggconfigd");
        return error;
    }
    if (method_error != GGL_ERR_OK) {
        GGL_LOGE("gghealthd", "component does not exist in registry");
        return GGL_ERR_NOENTRY;
    }
    if (result.type == GGL_TYPE_BUF) {
        GGL_LOGT(
            "gghealthd",
            "read %.*s",
            (int) result.buf.len,
            (const char *) result.buf.data
        );
    }
    return GGL_ERR_OK;
}
