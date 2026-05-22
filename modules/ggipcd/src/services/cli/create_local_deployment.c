// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "../../ipc_authz.h"
#include "../../ipc_error.h"
#include "../../ipc_server.h"
#include "../../ipc_service.h"
#include "cli.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/json_decode.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/client.h>
#include <stdint.h>
#include <stdlib.h>

// Convert a single per-component value from the external Smithy form to
// the canonical internal form (a map with optional "merge" and "reset"
// keys). Accepts:
//   (a) GG_TYPE_MAP with MERGE/merge and/or RESET/reset keys.
//   (b) GG_TYPE_BUF JSON string: {"merge":{...},"reset":[...]}.
// On success, *comp_pair's value is replaced with the canonical map.
static GgError normalize_component_config(GgKV *comp_pair, GgArena *alloc) {
    GgObject *val = gg_kv_val(comp_pair);

    if (gg_obj_type(*val) == GG_TYPE_MAP) {
        // External map form: normalize MERGE→merge, RESET→reset in place.
        GG_MAP_FOREACH (op, gg_obj_into_map(*val)) {
            if (gg_buffer_eq(gg_kv_key(*op), GG_STR("MERGE"))) {
                gg_kv_set_key(op, GG_STR("merge"));
            } else if (gg_buffer_eq(gg_kv_key(*op), GG_STR("RESET"))) {
                gg_kv_set_key(op, GG_STR("reset"));
            }
        }
        // Also parse string values of merge/reset into maps/lists.
        GG_MAP_FOREACH (op2, gg_obj_into_map(*val)) {
            GgObject *op_val = gg_kv_val(op2);
            if (gg_obj_type(*op_val) == GG_TYPE_BUF) {
                GgObject parsed;
                GgError parse_ret = gg_json_decode_destructive(
                    gg_obj_into_buf(*op_val), alloc, &parsed
                );
                if (parse_ret != GG_ERR_OK) {
                    GG_LOGE(
                        "Failed to parse componentToConfiguration %.*s value "
                        "as JSON.",
                        (int) gg_kv_key(*op2).len,
                        gg_kv_key(*op2).data
                    );
                    return GG_ERR_INVALID;
                }
                *op_val = parsed;
            }
        }
        return GG_ERR_OK;
    }

    if (gg_obj_type(*val) == GG_TYPE_BUF) {
        // JSON-string form: decode and replace the value with the parsed
        // map (canonical form).
        GgObject parsed;
        GgError ret
            = gg_json_decode_destructive(gg_obj_into_buf(*val), alloc, &parsed);
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to parse componentToConfiguration JSON value.");
            return GG_ERR_INVALID;
        }
        if (gg_obj_type(parsed) != GG_TYPE_MAP) {
            GG_LOGE("componentToConfiguration JSON value is not an object.");
            return GG_ERR_INVALID;
        }
        *val = parsed;
        return GG_ERR_OK;
    }

    GG_LOGE("componentToConfiguration per-component value has invalid type.");
    return GG_ERR_INVALID;
}

GgError ggl_handle_create_local_deployment(
    const GglIpcOperationInfo *info,
    GgMap args,
    uint32_t handle,
    int32_t stream_id,
    GglIpcError *ipc_error,
    GgArena *alloc
) {
    GG_MAP_FOREACH (pair, args) {
        if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("recipeDirectoryPath"))) {
            gg_kv_set_key(pair, GG_STR("recipe_directory_path"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("artifactsDirectoryPath")
                   )) {
            gg_kv_set_key(pair, GG_STR("artifacts_directory_path"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("rootComponentVersionsToAdd")
                   )) {
            gg_kv_set_key(pair, GG_STR("root_component_versions_to_add"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("rootComponentVersionsToRemove")
                   )) {
            gg_kv_set_key(pair, GG_STR("root_component_versions_to_remove"));
        } else if (gg_buffer_eq(
                       gg_kv_key(*pair), GG_STR("componentToConfiguration")
                   )) {
            gg_kv_set_key(pair, GG_STR("component_to_configuration"));
            // Convert each per-component value from the external Smithy
            // form to the canonical internal form at the IPC boundary.
            if (gg_obj_type(*gg_kv_val(pair)) == GG_TYPE_MAP) {
                GG_MAP_FOREACH (comp, gg_obj_into_map(*gg_kv_val(pair))) {
                    GgError conv_ret = normalize_component_config(comp, alloc);
                    if (conv_ret != GG_ERR_OK) {
                        *ipc_error = (GglIpcError) {
                            .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                            .message
                            = GG_STR("Invalid componentToConfiguration value.")
                        };
                        return conv_ret;
                    }
                }
            }
        } else if (gg_buffer_eq(gg_kv_key(*pair), GG_STR("groupName"))) {
            gg_kv_set_key(pair, GG_STR("group_name"));
        } else {
            GG_LOGE(
                "Unhandled argument: %.*s",
                (int) gg_kv_key(*pair).len,
                gg_kv_key(*pair).data
            );
        }
    }

    GgError ret
        = ggl_ipc_auth(info, GG_STR(""), ggl_ipc_default_policy_matcher);
    if (ret != GG_ERR_OK) {
        GG_LOGE("IPC Operation not authorized.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("IPC Operation not authorized.") };
        return GG_ERR_INVALID;
    }

    GgObject result;
    ret = ggl_call(
        GG_STR("gg_deployment"),
        GG_STR("create_local_deployment"),
        args,
        NULL,
        alloc,
        &result
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create local deployment.");
        *ipc_error = (GglIpcError
        ) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
            .message = GG_STR("Failed to create local deployment.") };
        return ret;
    }

    if (gg_obj_type(result) != GG_TYPE_BUF) {
        GG_LOGE("Received deployment ID not a string.");
        *ipc_error = (GglIpcError) { .error_code = GGL_IPC_ERR_SERVICE_ERROR,
                                     .message = GG_STR("Internal error.") };
        return GG_ERR_FAILURE;
    }

    return ggl_ipc_response_send(
        handle,
        stream_id,
        GG_STR("aws.greengrass#CreateLocalDeploymentResponse"),
        GG_MAP(gg_kv(GG_STR("deploymentId"), result))
    );
}
