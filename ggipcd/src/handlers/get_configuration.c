// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../ipc_server.h"
#include "handlers.h"
#include <ggl/alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>

// make_key_path_object is in update_configuration.c
extern GglObject *make_key_path_object(
    GglObject *component_name_object, GglObject *key_path_object
);

GglError handle_get_configuration(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
#if GGL_LOG_LEVEL == GGL_LOG_TRACE
    for (size_t x = 0; x < args.len; x++) {
        GglKV *kv = &args.pairs[x];
        GglBuffer *key = &kv->key;
        GGL_LOGT(
            "GetConfiguration",
            "found key : %.*s",
            (int) key->len,
            (char *) key->data
        );
    }
#endif
    GglObject *key_path_object;
    bool found = ggl_map_get(args, GGL_STR("keyPath"), &key_path_object);
    if (!found) {
        GGL_LOGE("UpdateConfiguration", "Missing keyPath.");
        return GGL_ERR_INVALID;
    }
    if (key_path_object->type != GGL_TYPE_LIST) {
        GGL_LOGE("UpdateConfiguration", "keyPath is not a List.");
        return GGL_ERR_INVALID;
    }

    GglObject component_name_object = GGL_OBJ_STR("component");

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          *make_key_path_object(&component_name_object, key_path_object) },
    );

    GglError error;
    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("read"),
        params,
        &error,
        alloc,
        &call_resp
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#UpdateConfigurationResponse"),
        call_resp
    );
}
