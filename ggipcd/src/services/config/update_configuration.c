// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "config.h"
#include "make_config_path_object.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
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
    GglAlloc *alloc
) {
    (void) info;
    (void) alloc;

    GglObject *key_path_obj;
    GglObject *component_name_obj;
    GglObject *value_to_merge_obj;
    GglObject *timestamp_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("keyPath"), true, GGL_TYPE_LIST, &key_path_obj },
            { GGL_STR("componentName"),
              false,
              GGL_TYPE_BUF,
              &component_name_obj },
            { GGL_STR("valueToMerge"),
              true,
              GGL_TYPE_NULL,
              &value_to_merge_obj },
            { GGL_STR("timestamp"), true, GGL_TYPE_F64, &timestamp_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("GetConfiguration", "Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    ret = ggl_list_type_check(key_path_obj->list, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("GetConfiguration", "Received invalid paramters.");
        return GGL_ERR_INVALID;
    }

    GglBuffer component_name;
    if (component_name_obj != NULL) {
        component_name = component_name_obj->buf;
    } else {
        ret = ggl_ipc_get_component_name(handle, &component_name);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    // convert timestamp from sec in floating-point(with msec precision) to msec
    // in integer
    int64_t timestamp = (int64_t) timestamp_obj->f64 * 1000;
    GGL_LOGT("UpdateConfiguration", "timestamp is %" PRId64, timestamp);

    GglBufList full_key_path;
    ret = ggl_make_config_path_object(
        component_name, key_path_obj->list, &full_key_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_gg_config_write(
        full_key_path.bufs, full_key_path.len, *value_to_merge_obj, timestamp
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    // TODO: return IPC errors:
    // https://github.com/awslabs/smithy-iot-device-sdk-greengrass-ipc/blob/60966747302e17eb8cc6ddad972f90aa92ad38a7/greengrass-ipc-model/main.smithy#L82

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#UpdateConfigurationResponse"),
        GGL_OBJ_MAP()
    );
}
