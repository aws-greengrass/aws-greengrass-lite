// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "config.h"
#include "config_path_object.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/flags.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_handle_get_configuration(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglArena *alloc
) {
    GglObject *key_path_obj;
    GglObject *component_name_obj;

    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("keyPath"), GGL_OPTIONAL, GGL_TYPE_LIST, &key_path_obj },
            { GGL_STR("componentName"),
              GGL_OPTIONAL,
              GGL_TYPE_BUF,
              &component_name_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters. Failed to validate the map.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    GglList key_path = { 0 };
    if (key_path_obj != NULL) {
        key_path = ggl_obj_into_list(*key_path_obj);
    }

    ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Received invalid parameters. keyPath is not a list of strings."
        );
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    GglBuffer component_name = info->component;
    if (component_name_obj != NULL) {
        component_name = ggl_obj_into_buf(*component_name_obj);
    }

    GglBufList full_key_path;
    ret = ggl_make_config_path_object(component_name, key_path, &full_key_path);
    if (ret != GGL_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Config path depth larger than supported.") };
        return ret;
    }

    GglObject read_value;
    ret = ggl_gg_config_read(full_key_path, alloc, &read_value);
    if (ret != GGL_ERR_OK) {
        if (ret == GGL_ERR_NOENTRY) {
            *ipc_error
                = (GglIpcError) { .error_code = GGL_IPC_ERR_RESOURCE_NOT_FOUND,
                                  .message = GGL_STR("Key not found.") };
        }
        return ret;
    }

    // According to the IPC spec, if keyPath has a valid value,
    //  For MAP values a map without the keyPath leaf is returned.
    //  For non-MAP values, a map with the keyPath leaf and the value is
    //  returned.
    GglKV wrapped_result = { 0 };
    GglObjectType read_type = ggl_obj_type(read_value);
    if (read_type != GGL_TYPE_MAP) {
        if (key_path.len > 0) {
            wrapped_result = ggl_kv(
                ggl_obj_into_buf(key_path.items[key_path.len - 1]), read_value
            );
            read_value
                = ggl_obj_map((GglMap) { .pairs = &wrapped_result, .len = 1 });
        } else {
            // A state where the whole configuration is requested but the result
            // is not a map then error.
            *ipc_error
                = (GglIpcError) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                                  .message = GGL_STR("Key is not valid.") };

            return GGL_ERR_CONFIG;
        }
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#GetConfigurationResponse"),
        GGL_MAP(
            ggl_kv(GGL_STR("componentName"), ggl_obj_buf(component_name)),
            ggl_kv(GGL_STR("value"), read_value)
        )
    );
}
