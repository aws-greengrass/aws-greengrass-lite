// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "config.h"
#include "config_path_object.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_handle_get_configuration(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    GgObject *key_path_obj;
    GgObject *component_name_obj;

    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("keyPath"), GG_OPTIONAL, GG_TYPE_LIST, &key_path_obj },
            { GG_STR("componentName"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &component_name_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters. Failed to validate the map.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GgList key_path = { 0 };
    if (key_path_obj != NULL) {
        key_path = gg_obj_into_list(*key_path_obj);
    }

    ret = gg_list_type_check(key_path, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters. keyPath is not a list of strings."
        );
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GgBuffer component_name = info->component;
    if (component_name_obj != NULL) {
        component_name = gg_obj_into_buf(*component_name_obj);
    }

    GgBufList full_key_path;
    ret = ggl_make_config_path_object(component_name, key_path, &full_key_path);
    if (ret != GG_ERR_OK) {
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Config path depth larger than supported.") };
        return ret;
    }

    GgObject read_value;
    ret = ggl_gg_config_read(full_key_path, alloc, &read_value);
    if (ret != GG_ERR_OK) {
        if (ret == GG_ERR_NOENTRY) {
            *ipc_error
                = (GglIpcError) { .error_code = GGL_IPC_ERR_RESOURCE_NOT_FOUND,
                                  .message = GG_STR("Key not found.") };
        }
        return ret;
    }

    // According to the IPC spec, if keyPath has a valid value,
    //  For MAP values a map without the keyPath leaf is returned.
    //  For non-MAP values, a map with the keyPath leaf and the value is
    //  returned.
    GgKV wrapped_result = { 0 };
    GgObjectType read_type = gg_obj_type(read_value);
    if (read_type != GG_TYPE_MAP) {
        if (key_path.len > 0) {
            wrapped_result = gg_kv(
                gg_obj_into_buf(key_path.items[key_path.len - 1]), read_value
            );
            read_value
                = gg_obj_map((GgMap) { .pairs = &wrapped_result, .len = 1 });
        } else {
            // A state where the whole configuration is requested but the result
            // is not a map then error.
            *ipc_error
                = (GglIpcError) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                                  .message = GG_STR("Key is not valid.") };

            return GG_ERR_CONFIG;
        }
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#GetConfigurationResponse"),
        GG_MAP(
            gg_kv(GG_STR("componentName"), gg_obj_buf(component_name)),
            gg_kv(GG_STR("value"), read_value)
        )
    );
}
