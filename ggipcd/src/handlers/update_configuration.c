// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../ipc_server.h"
#include "handlers.h"
#include "make_key_path_object.h"
#include <ggl/alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>

GglError handle_update_configuration(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
#if GGL_LOG_LEVEL == GGL_LOG_TRACE
    for (size_t x = 0; x < args.len; x++) {
        GglKV *kv = &args.pairs[x];
        GglBuffer *key = &kv->key;
        GGL_LOGT(
            "UpdateConfiguration",
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
    // TODO: get the calling component name

    GglObject *value_to_merge_object;
    found = ggl_map_get(args, GGL_STR("valueToMerge"), &value_to_merge_object);
    if (!found) {
        GGL_LOGE("UpdateConfiguration", "Missing valueToMerge.");
        return GGL_ERR_INVALID;
    }
    // valueToMerge should be allowed to be ANY object

    GglObject *time_stamp_object;
    found = ggl_map_get(args, GGL_STR("timestamp"), &time_stamp_object);
    if (!found) {
        GGL_LOGE("UpdateConfiguration", "Missing timestamp.");
        return GGL_ERR_INVALID;
    }
    if (time_stamp_object->type != GGL_TYPE_F64) {
        GGL_LOGE(
            "UpdateConfiguration",
            "timestamp is %d not a F64",
            time_stamp_object->type
        );
        return GGL_ERR_INVALID;
    }
    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          *make_key_path_object(&component_name_object, key_path_object) },
        { GGL_STR("value"), *value_to_merge_object },
        { GGL_STR("timestamp"), *time_stamp_object }
    );

    GglError error;
    GglObject call_resp;
    GglError ret = ggl_call(
        GGL_STR("/aws/ggl/ggconfigd"),
        GGL_STR("write"),
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
        GGL_OBJ_MAP()
    );
}
