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
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

static GglError subscribe_to_configuration_update_callback(
    GglObject data, uint32_t resp_handle, int32_t stream_id, GglAlloc *alloc
) {
    (void) alloc;

    if (data.type != GGL_TYPE_LIST) {
        GGL_LOGE(
            "SubscribeToConfigurationUpdate",
            "Received invalid subscription response, expected a List."
        );
        return GGL_ERR_FAILURE;
    }

    GglBuffer component_name;
    GglList key_path;

    GglError err = ggl_parse_config_path(data.list, &component_name, &key_path);
    if (err != GGL_ERR_OK) {
        return err;
    }

    GglObject ipc_response = GGL_OBJ_MAP(
        { GGL_STR("configurationUpdateEvent"),
          GGL_OBJ_MAP(
              { GGL_STR("componentName"), GGL_OBJ(component_name) },
              { GGL_STR("keyPath"), GGL_OBJ(key_path) },
          ) },
    );

    err = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GGL_STR("aws.greengrass#ConfigurationUpdateEvents"),
        ipc_response
    );
    if (err != GGL_ERR_OK) {
        GGL_LOGE(
            "SubscribeToConfigurationUpdate",
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
    GglAlloc *alloc
) {
    (void) info;
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
        GGL_LOGE(
            "SubscribeToConfigurationUpdate", "Received invalid paramters."
        );
        return GGL_ERR_INVALID;
    }

    // An empty key path list implies we want to subscribe to all keys under
    // this component's configuration. Similarly, (although this doesn't appear
    // to be documented) no key path provided also implies we want to subscribe
    // to all keys under this component's configuration
    GglObject *empty_list = (GglObject *) &GGL_OBJ_LIST();
    if (key_path_obj == NULL) {
        key_path_obj = empty_list;
    } else {
        ret = ggl_list_type_check(key_path_obj->list, GGL_TYPE_BUF);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE(
                "SubscribeToConfigurationUpdate", "Received invalid paramters."
            );
            return GGL_ERR_INVALID;
        }
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

    GglBufList full_key_path;
    ret = ggl_make_config_path_object(
        component_name, key_path_obj->list, &full_key_path
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject config_path_obj[GGL_MAX_CONFIG_DEPTH] = { 0 };
    for (size_t i = 0; i < full_key_path.len; i++) {
        config_path_obj[i] = GGL_OBJ(full_key_path.bufs[i]);
    }

    GglMap call_args = GGL_MAP(
        { GGL_STR("key_path"),
          GGL_OBJ((GglList) { .items = config_path_obj,
                              .len = full_key_path.len }) },
    );

    // TODO: return IPC errors
    // TODO: handle remote error
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
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GGL_STR("aws.greengrass#SubscribeToConfigurationUpdateResponse"),
        GGL_OBJ_MAP()
    );
}
