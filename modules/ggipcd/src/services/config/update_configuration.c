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
#include <inttypes.h>
#include <stddef.h>

GgError ggl_handle_update_configuration(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;

    GgObject *key_path_obj = NULL;
    GgObject *value_to_merge;
    GgObject *timestamp_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("keyPath"), GG_OPTIONAL, GG_TYPE_LIST, &key_path_obj },
            { GG_STR("valueToMerge"),
              GG_REQUIRED,
              GG_TYPE_NULL,
              &value_to_merge },
            { GG_STR("timestamp"), GG_REQUIRED, GG_TYPE_F64, &timestamp_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
            .message = GG_STR("Received invalid parameters.") };
        return GG_ERR_INVALID;
    }

    GgList key_path = { 0 };
    if (key_path_obj != NULL) {
        key_path = gg_obj_into_list(*key_path_obj);
        ret = gg_list_type_check(key_path, GG_TYPE_BUF);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Received invalid parameters.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                .message = GG_STR("Received invalid parameters.") };
            return GG_ERR_INVALID;
        }

        if ((key_path.len >= 1)
            && gg_buffer_eq(
                gg_obj_into_buf(key_path.items[0]), GG_STR("accessControl")
            )) {
            GG_LOGE(
                "Received invalid parameters. Can not change component accessControl over IPC."
            );
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                .message = GG_STR(
                    "Config update is not allowed for following field [accessControl]"
                ) };
            return GG_ERR_INVALID;
        }
    }

    if ((key_path.len == 0) && gg_obj_type(*value_to_merge) == GG_TYPE_MAP) {
        GG_MAP_FOREACH (kv, gg_obj_into_map(*value_to_merge)) {
            if (gg_buffer_eq(gg_kv_key(*kv), GG_STR("accessControl"))) {
                GG_LOGE(
                    "Received invalid parameters. Can not change component accessControl over IPC."
                );
                *ipc_error = (GglIpcError
                ) { .error_code = GGL_IPC_ERR_INVALID_ARGUMENTS,
                    .message = GG_STR(
                        "Config update is not allowed for following field [accessControl]"
                    ) };
                return GG_ERR_INVALID;
            }
        }
    }

    // convert timestamp from sec in floating-point(with msec precision) to msec
    // in integer
    int64_t timestamp = (int64_t) (gg_obj_into_f64(*timestamp_obj) * 1000.0);
    GG_LOGT("Timestamp is %" PRId64, timestamp);

    GgBufList full_key_path;
    ret = ggl_make_config_path_object(
        info->component, key_path, &full_key_path
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Config path depth larger than supported.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Config path depth larger than supported.") };
        return ret;
    }

    ret = ggl_gg_config_write(full_key_path, *value_to_merge, &timestamp);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to update the configuration.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to update the configuration.") };
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#UpdateConfigurationResponse"),
        (GgMap) { 0 }
    );
}
