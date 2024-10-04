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
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

GglError ggl_handle_get_configuration(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglAlloc *alloc
) {
    (void) info;

    GglObject *key_path_obj;
    GglObject *component_name_obj;
    GglBuffer component_name;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("keyPath"), true, true, GGL_TYPE_LIST, &key_path_obj },
            { GGL_STR("componentName"),
              false,
              true,
              GGL_TYPE_BUF,
              &component_name_obj },
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

    if (component_name_obj != NULL) {
        component_name = component_name_obj->buf;
    } else {
        ret = ggl_ipc_get_component_name(handle, &component_name);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    GglBufList full_key_path;
    ret = ggl_make_config_path_object(
        component_name, key_path_obj->list, &full_key_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject read_value;
    ret = ggl_gg_config_read(full_key_path, alloc, &read_value);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    // TODO: return IPC errors:
    // https://github.com/awslabs/smithy-iot-device-sdk-greengrass-ipc/blob/60966747302e17eb8cc6ddad972f90aa92ad38a7/greengrass-ipc-model/main.smithy#L74

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#GetConfigurationResponse"),
        GGL_OBJ_MAP(
            { GGL_STR("componentName"), GGL_OBJ(component_name) },
            { GGL_STR("value"), read_value }
        )
    );
}
