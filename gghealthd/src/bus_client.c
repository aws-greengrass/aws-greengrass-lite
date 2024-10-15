// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_client.h"
#include <sys/types.h>
#include <assert.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>

static GglError get_key(GglAlloc *alloc, GglList key_path, GglObject *result) {
    GglBuffer server = GGL_STR("/aws/ggl/ggconfigd");
    GglMap params = GGL_MAP({ GGL_STR("key_path"), GGL_OBJ(key_path) }, );

    GglError method_error = GGL_ERR_OK;
    GglError error = ggl_call(
        server, GGL_STR("read"), params, &method_error, alloc, result
    );
    if (error != GGL_ERR_OK) {
        GGL_LOGE("failed to connect to ggconfigd");
        return error;
    }
    if (method_error != GGL_ERR_OK) {
        GGL_LOGE("component does not exist in registry");
        return GGL_ERR_NOENTRY;
    }
    return GGL_ERR_OK;
}

static pthread_mutex_t bump_alloc_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t bump_buffer[4096];

// Check a component's version field in ggconfigd for proof of existence
GglError verify_component_exists(GglBuffer component_name) {
    int ret = pthread_mutex_lock(&bump_alloc_mutex);
    if (ret < 0) {
        GGL_LOGE("failed to lock mutex (errno = %d)", errno);
        return GGL_ERR_FAILURE;
    }
    GGL_DEFER(pthread_mutex_unlock, bump_alloc_mutex);

    GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(bump_buffer));

    return get_key(
        &alloc.alloc,
        GGL_LIST(
            GGL_OBJ_STR("services"),
            GGL_OBJ(component_name),
            GGL_OBJ_STR("version")
        ),
        &(GglObject) { 0 }
    );
}

GglError get_root_component_list(GglAlloc *alloc, GglList *component_list) {
    assert(
        (alloc != NULL) && (component_list != NULL)
        && (component_list->items == NULL)
    );

    GglObject result = { 0 };
    GglError err = get_key(
        alloc,
        GGL_LIST(
            GGL_OBJ_STR("services"),
            GGL_OBJ_STR("main"),
            GGL_OBJ_STR("dependencies")
        ),
        &result
    );
    if (err != GGL_ERR_OK) {
        return err;
    }
    if (result.type != GGL_TYPE_LIST) {
        GGL_LOGE("ggconfigd protocol error expected Map");
        return GGL_ERR_FATAL;
    }
    *component_list = result.list;

    return GGL_ERR_OK;
}
