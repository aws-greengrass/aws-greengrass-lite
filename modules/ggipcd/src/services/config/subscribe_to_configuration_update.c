// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
#include "config.h"
#include "config_path_object.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/constants.h>
#include <ggl/error.h>
#include <ggl/ipc/common.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError subscribe_to_configuration_update_callback(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    if (ggl_obj_type(data) != GGL_TYPE_LIST) {
        GGL_LOGE("Received invalid subscription response, expected a List.");
        return GGL_ERR_FAILURE;
    }

    GglBuffer component_name;
    GglList key_path;

    GglError err = ggl_parse_config_path(
        ggl_obj_into_list(data), &component_name, &key_path
    );
    if (err != GGL_ERR_OK) {
        return err;
    }

    GglObject ipc_response = ggl_obj_map(GGL_MAP(
        { GGL_STR("configurationUpdateEvent"),
          ggl_obj_map(GGL_MAP(
              { GGL_STR("componentName"), ggl_obj_buf(component_name) },
              { GGL_STR("keyPath"), ggl_obj_list(key_path) },
          )) },
    ));

    err = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GGL_STR("aws.greengrass#ConfigurationUpdateEvents"),
        ipc_response
    );
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to send subscription response with error %s; skipping.",
            ggl_strerror(err)
        );
    }

    return GGL_ERR_OK;
}

GglError ggl_handle_subscribe_to_configuration_update(
    const GglIpcOperationInfo *info,
    GglMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GglAlloc *alloc
) {
    (void) alloc;

    GglObject *key_path_obj;
    GglObject *component_name_obj;
    GglError ret = ggl_map_validate(
        args,
        GGL_MAP_SCHEMA(
            { GGL_STR("componentName"),
              false,
              GGL_TYPE_BUF,
              &component_name_obj },
            { GGL_STR("keyPath"), false, GGL_TYPE_LIST, &key_path_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GGL_STR("Failed to validate the map.") };
        return GGL_ERR_INVALID;
    }

    // An empty key path list implies we want to subscribe to all keys under
    // this component's configuration. Similarly, (although this doesn't appear
    // to be documented) no key path provided also implies we want to subscribe
    // to all keys under this component's configuration
    GglList key_path = { 0 };
    if (key_path_obj != NULL) {
        key_path = ggl_obj_into_list(*key_path_obj);
        ret = ggl_list_type_check(key_path, GGL_TYPE_BUF);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Received invalid parameters. keyPath must be a list of "
                     "strings.");
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GGL_STR("Received invalid parameters: keyPath must "
                                   "be list of strings.") };
            return GGL_ERR_INVALID;
        }
    }

    GglBuffer component_name = info->component;
    if (component_name_obj != NULL) {
        component_name = ggl_obj_into_buf(*component_name_obj);
    }

    GglBufList full_key_path;
    ret = ggl_make_config_path_object(component_name, key_path, &full_key_path);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Config path depth larger than supported.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GGL_STR("Config path depth larger than supported.") };
        return ret;
    }

    GglObject config_path_obj[GGL_MAX_OBJECT_DEPTH] = { 0 };
    for (size_t i = 0; i < full_key_path.len; i++) {
        config_path_obj[i] = ggl_obj_buf(full_key_path.bufs[i]);
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("key_path"),
          ggl_obj_list((GglList) { .items = config_path_obj,
                                   .len = full_key_path.len }) },
    );

    GglError remote_err;
    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GGL_STR("gg_config"),
        GGL_STR("subscribe"),
        call_args,
        subscribe_to_configuration_update_callback,
        &remote_err
    );
    if (ret != GGL_ERR_OK) {
        if (remote_err == GGL_ERR_NOENTRY) {
            *ipc_error
                = (GglIpcError) { .error_code = GGL_IPC_ERR_RESOURCE_NOT_FOUND,
                                  .message = GGL_STR("Key not found") };
        } else {
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message
                = GGL_STR("Failed to subscribe to configuration update.") };
        }
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToConfigurationUpdateResponse"),
        ggl_obj_map((GglMap) { 0 })
    );
}
