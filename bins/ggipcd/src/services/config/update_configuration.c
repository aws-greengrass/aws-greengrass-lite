// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "config.h"
#include "config_path_object.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

GglError ggl_handle_update_configuration(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglAlloc *alloc
) {
    (void) alloc;

    GglObject *key_path_obj = NULL;
    GglObject *value_to_merge_obj;
    GglObject *timestamp_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("keyPath"), false, GGL_TYPE_LIST, &key_path_obj },
            { GGL_STR("valueToMerge"),
              true,
              GGL_TYPE_NULL,
              &value_to_merge_obj },
            { GGL_STR("timestamp"), true, GGL_TYPE_F64, &timestamp_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
            .message = GGL_STR("Received invalid parameters.") };
        return GGL_ERR_INVALID;
    }

    GglObject *empty_list = (GglObject *) &GGL_OBJ_LIST({ 0 });
    if (key_path_obj == NULL) {
        key_path_obj = empty_list;
    } else {
        ret = ggl_list_type_check(key_path_obj->list, GGL_TYPE_BUF);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Received invalid parameters.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                .message = GGL_STR("Received invalid parameters.") };
            return GGL_ERR_INVALID;
        }

        if ((key_path_obj->list.len >= 1)
            && ggl_buffer_eq(
                key_path_obj->list.items[0].buf, GGL_STR("accessControl")
            )) {
            GGL_LOGE("Received invalid parameters. Can not change component "
                     "accessControl over IPC.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                .message = GGL_STR("Config update is not allowed for following "
                                   "field [accessControl]") };
            return GGL_ERR_INVALID;
        }
    }

    if ((key_path_obj->list.len == 0)
        && value_to_merge_obj->type == GGL_TYPE_MAP) {
        for (size_t i = 0; i < value_to_merge_obj->map.len; i++) {
            if (ggl_buffer_eq(
                    value_to_merge_obj->map.pairs[i].key,
                    GGL_STR("accessControl")
                )) {
                GGL_LOGE(
                    "Received invalid parameters. Can not change component "
                    "accessControl over IPC."
                );
                *ipc_error = (GglIpcError
                ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                    .message = GGL_STR("Config update is not allowed for "
                                       "following field [accessControl]") };
                return GGL_ERR_INVALID;
            }
        }
    }

    // convert timestamp from sec in floating-point(with msec precision) to msec
    // in integer
    int64_t timestamp = (int64_t) timestamp_obj->f64 * 1000;
    GGL_LOGT("Timestamp is %" PRId64, timestamp);

    GglBufList full_key_path;
    ret = ggl_make_config_path_object(
        info->component, key_path_obj->list, &full_key_path
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Config path depth larger than supported.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Config path depth larger than supported.") };
        return ret;
    }

    ret = ggl_gg_config_write(full_key_path, *value_to_merge_obj, &timestamp);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to update the configuration.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Failed to update the configuration.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#UpdateConfigurationResponse"),
        GGL_OBJ_MAP({ 0 })
    );
}
