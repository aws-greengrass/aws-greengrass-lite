// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_authz.h"
#include "ipc_service.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <ggl/core_bus/gg_config.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GgError policy_match(
    GgMap policy,
    GgBuffer operation,
    GgBuffer resource,
    GglIpcPolicyResourceMatcher *matcher
) {
    GgObject *operations_obj;
    GgObject *resources_obj;
    GgError ret = gg_map_validate(
        policy,
        GG_MAP_SCHEMA(
            { GG_STR("operations"),
              GG_REQUIRED,
              GG_TYPE_LIST,
              &operations_obj },
            { GG_STR("resources"), GG_REQUIRED, GG_TYPE_LIST, &resources_obj },
        )
    );
    if (ret != GG_ERR_OK) {
        return GG_ERR_CONFIG;
    }
    GgList policy_operations = gg_obj_into_list(*operations_obj);
    GgList policy_resources = gg_obj_into_list(*resources_obj);

    ret = gg_list_type_check(policy_operations, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        return GG_ERR_CONFIG;
    }
    ret = gg_list_type_check(policy_resources, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        return GG_ERR_CONFIG;
    }

    GG_LIST_FOREACH (policy_operation_obj, policy_operations) {
        GgBuffer policy_operation = gg_obj_into_buf(*policy_operation_obj);
        if (gg_buffer_eq(GG_STR("*"), policy_operation)
            || gg_buffer_eq(operation, policy_operation)) {
            GG_LIST_FOREACH (policy_resource_obj, policy_resources) {
                GgBuffer policy_resource
                    = gg_obj_into_buf(*policy_resource_obj);
                if (gg_buffer_eq(GG_STR("*"), policy_resource)
                    || matcher(resource, policy_resource)) {
                    return GG_ERR_OK;
                }
            }
            return GG_ERR_FAILURE;
        }
    }

    return GG_ERR_NOENTRY;
}

GgError ggl_ipc_auth(
    const GglIpcOperationInfo *info,
    GgBuffer resource,
    GglIpcPolicyResourceMatcher *matcher
) {
    assert(info != NULL);

    static uint8_t policy_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(policy_mem));

    GgObject policies;
    GgError ret = ggl_gg_config_read(
        GG_BUF_LIST(
            GG_STR("services"),
            info->component,
            GG_STR("configuration"),
            GG_STR("accessControl"),
            info->service
        ),
        &alloc,
        &policies
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to get policies for service %.*s in component %.*s.",
            (int) info->service.len,
            info->service.data,
            (int) info->component.len,
            info->component.data
        );
        return ret;
    }

    if (gg_obj_type(policies) != GG_TYPE_MAP) {
        GG_LOGE("Configuration's accessControl is not a map.");
        return GG_ERR_CONFIG;
    }

    GgMap policy_map = gg_obj_into_map(policies);

    GG_MAP_FOREACH (policy_kv, policy_map) {
        GgObject policy = *gg_kv_val(policy_kv);
        if (gg_obj_type(policy) != GG_TYPE_MAP) {
            GG_LOGE("Policy value is not a map.");
            return GG_ERR_CONFIG;
        }

        ret = policy_match(
            gg_obj_into_map(policy), info->operation, resource, matcher
        );
        if (ret == GG_ERR_OK) {
            return GG_ERR_OK;
        }
    }

    return GG_ERR_NOENTRY;
}

bool ggl_ipc_default_policy_matcher(
    GgBuffer request_resource, GgBuffer policy_resource
) {
    GgBuffer pattern = policy_resource;
    bool in_escape = false;
    size_t write_pos = 0;
    for (size_t i = 0; i < pattern.len; i++) {
        uint8_t c = pattern.data[i];
        if (in_escape) {
            if (c == (uint8_t) '}') {
                in_escape = false;
                continue;
            }
        } else {
            if (c == (uint8_t) '*') {
                pattern.data[write_pos] = (uint8_t) '\0';
                write_pos += 1;
                continue;
            }
            if ((c == (uint8_t) '$') && (i < pattern.len - 1)
                && (pattern.data[i + 1] == (uint8_t) '{')) {
                in_escape = true;
                i += 1;
                continue;
            }
        }

        pattern.data[write_pos] = c;
        write_pos += 1;
    }
    pattern.len = write_pos;

    GgBuffer remaining = request_resource;
    size_t start = 0;
    for (size_t i = 0; i < pattern.len; i++) {
        if (pattern.data[i] == (uint8_t) '\0') {
            GgBuffer segment = gg_buffer_substr(pattern, start, i);
            bool match;
            size_t match_start = 0;
            if (start == 0) {
                match = gg_buffer_has_prefix(remaining, segment);
            } else {
                match = gg_buffer_contains(remaining, segment, &match_start);
            }
            if (!match) {
                return false;
            }
            remaining = gg_buffer_substr(
                remaining, match_start + segment.len, SIZE_MAX
            );
            start = i + 1;
        }
    }

    if (start == 0) {
        return gg_buffer_eq(remaining, pattern);
    }
    GgBuffer segment = gg_buffer_substr(pattern, start, SIZE_MAX);
    return gg_buffer_has_suffix(remaining, segment);
}
