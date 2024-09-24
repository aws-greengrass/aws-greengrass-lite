// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../ipc_server.h"
#include "handlers.h"
#include "make_config_path_object.h"
#include <ggl/alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

GglError handle_get_configuration(
    GglMap args, uint32_t handle, int32_t stream_id, GglAlloc *alloc
) {
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
    GglObject *key_path_object;
    bool found = ggl_map_get(args, GGL_STR("keyPath"), &key_path_object);
    if (!found) {
        GGL_LOGE("GetConfiguration", "Missing keyPath.");
        return GGL_ERR_INVALID;
    }
    if (key_path_object->type != GGL_TYPE_LIST) {
        GGL_LOGE("GetConfiguration", "keyPath is not a List.");
        return GGL_ERR_INVALID;
    }

    GglBuffer component_name_buffer;
    GglObject component_name_object;
    GglObject *component_name_object_ptr = &component_name_object;
    found = ggl_map_get(
        args, GGL_STR("componentName"), &component_name_object_ptr
    );
    if (found) {
        if (key_path_object->type != GGL_TYPE_LIST) {
            GGL_LOGE("GetConfiguration", "keyPath is not a List.");
            return GGL_ERR_INVALID;
        }
    } else {
        GglError err
            = ggl_ipc_get_component_name(handle, &component_name_buffer);
        if (err != GGL_ERR_OK) {
            return err;
        }
        component_name_object = GGL_OBJ(component_name_buffer);
        component_name_object_ptr = &component_name_object;
    }
    GGL_LOGT(
        "GetConfiguration",
        "Component Name : %.*s",
        (int) component_name_object_ptr->buf.len,
        (char *) component_name_object_ptr->buf.data
    );

    GglMap params = GGL_MAP(
        { GGL_STR("key_path"),
          *ggl_make_config_path_object(
              component_name_object_ptr, key_path_object
          ) },
    );
    GglError remote_error;
    GglObject core_bus_response;
    GglError err = ggl_call(
        GGL_STR("gg_config"),
        GGL_STR("read"),
        params,
        &remote_error,
        alloc,
        &core_bus_response
    );
    if (err != GGL_ERR_OK) {
        return err;
    }
    // TODO: handle remote_error
    // TODO: return IPC errors:
    // https://github.com/awslabs/smithy-iot-device-sdk-greengrass-ipc/blob/60966747302e17eb8cc6ddad972f90aa92ad38a7/greengrass-ipc-model/main.smithy#L74

    GglObject response = GGL_OBJ_MAP(
        { GGL_STR("componentName"), *component_name_object_ptr },
        { GGL_STR("value"), core_bus_response }
    );

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#GetConfigurationResponse"),
        response
    );
}
