// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "../../ipc_subscriptions.h"
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
#include <stddef.h>
#include <stdint.h>

static GgError subscribe_to_configuration_update_callback(
    GgObject data, uint32_t resp_handle, int32_t stream_id, GgArena *alloc
) {
    (void) alloc;

    if (gg_obj_type(data) != GG_TYPE_LIST) {
        GG_LOGE("Received invalid subscription response, expected a List.");
        return GG_ERR_FAILURE;
    }

    GgBuffer component_name;
    GgList key_path;

    GgError err = ggl_parse_config_path(
        gg_obj_into_list(data), &component_name, &key_path
    );
    if (err != GG_ERR_OK) {
        return err;
    }

    GgMap ipc_response = GG_MAP(
        gg_kv(
            GG_STR("configurationUpdateEvent"),
            gg_obj_map(GG_MAP(
                gg_kv(GG_STR("componentName"), gg_obj_buf(component_name)),
                gg_kv(GG_STR("keyPath"), gg_obj_list(key_path)),
            ))
        ),
    );

    err = ggl_ipc_response_send(
        resp_handle,
        stream_id,
        GG_STR("aws.greengrass#ConfigurationUpdateEvents"),
        ipc_response
    );
    if (err != GG_ERR_OK) {
        GG_LOGE(
            "Failed to send subscription response with error %s; skipping.",
            gg_strerror(err)
        );
    }

    return GG_ERR_OK;
}

GgError ggl_handle_subscribe_to_configuration_update(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    (void) alloc;

    GgObject *key_path_obj;
    GgObject *component_name_obj;
    GgError ret = gg_map_validate(
        args,
        GG_MAP_SCHEMA(
            { GG_STR("componentName"),
              GG_OPTIONAL,
              GG_TYPE_BUF,
              &component_name_obj },
            { GG_STR("keyPath"), GG_OPTIONAL, GG_TYPE_LIST, &key_path_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Received invalid parameters.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message
                                     = GG_STR("Failed to validate the map.") };
        return GG_ERR_INVALID;
    }

    // An empty key path list implies we want to subscribe to all keys under
    // this component's configuration. Similarly, (although this doesn't appear
    // to be documented) no key path provided also implies we want to subscribe
    // to all keys under this component's configuration
    GgList key_path = { 0 };
    if (key_path_obj != NULL) {
        key_path = gg_obj_into_list(*key_path_obj);
        ret = gg_list_type_check(key_path, GG_TYPE_BUF);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Received invalid parameters. keyPath must be a list of strings."
            );
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message = GG_STR(
                    "Received invalid parameters: keyPath must be list of strings."
                ) };
            return GG_ERR_INVALID;
        }
    }

    GgBuffer component_name = info->component;
    if (component_name_obj != NULL) {
        component_name = gg_obj_into_buf(*component_name_obj);
    }

    GgBufList full_key_path;
    ret = ggl_make_config_path_object(component_name, key_path, &full_key_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Config path depth larger than supported.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Config path depth larger than supported.") };
        return ret;
    }

    // Increase buffer size to prevent _FORTIFY_SOURCE stringop-overflow
    // _FORTIFY_SOURCE detected write at offset 186, need minimum 187 bytes
    // GG_MAX_OBJECT_DEPTH (15) + 2 = 17 elements * 11 bytes = 187 bytes
    GgObject config_path_obj[GG_MAX_OBJECT_DEPTH + 2] = { 0 };
    for (size_t i = 0; i < full_key_path.len; i++) {
        config_path_obj[i] = gg_obj_buf(full_key_path.bufs[i]);
    }

    GgMap call_args = GG_MAP(
        gg_kv(
            GG_STR("key_path"),
            gg_obj_list((GgList) { .items = config_path_obj,
                                   .len = full_key_path.len })
        ),
    );

    GgError remote_err;
    ret = ggl_ipc_bind_subscription(
        handle,
        stream_id,
        GG_STR("gg_config"),
        GG_STR("subscribe"),
        call_args,
        subscribe_to_configuration_update_callback,
        &remote_err
    );
    if (ret != GG_ERR_OK) {
        if ((ret == GG_ERR_REMOTE) && (remote_err == GG_ERR_NOENTRY)) {
            *ipc_error
                = (GglIpcError) { .error_code = GGL_IPC_ERR_RESOURCE_NOT_FOUND,
                                  .message = GG_STR("Key not found") };
        } else {
            *ipc_error = (GglIpcError
            ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                .message
                = GG_STR("Failed to subscribe to configuration update.") };
        }
        return ret;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#SubscribeToConfigurationUpdateResponse"),
        (GgMap) { 0 }
    );
}
