// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_authz.h"
#include "ipc_service.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static GglError policy_match(
    GglMap policy,
    GglBuffer operation,
    GglBuffer resource,
    GglIpcPolicyResourceMatcher *matcher
) {
    GglObject *operations_obj;
    GglObject *resources_obj;
    GglError ret = ggl_map_validate(
        policy,
        GGL_MAP_SCHEMA(
            { GGL_STR("operations"), true, GGL_TYPE_LIST, &operations_obj },
            { GGL_STR("resources"), true, GGL_TYPE_LIST, &resources_obj },
        )
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_CONFIG;
    }
    GglList policy_operations = operations_obj->list;
    GglList policy_resources = resources_obj->list;

    ret = ggl_list_type_check(policy_operations, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_CONFIG;
    }
    ret = ggl_list_type_check(policy_resources, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_CONFIG;
    }

    GGL_LIST_FOREACH(policy_operation, policy_operations) {
        if (ggl_buffer_eq(operation, policy_operation->buf)) {
            GGL_LIST_FOREACH(policy_resource, policy_resources) {
                if (ggl_buffer_eq(GGL_STR("*"), policy_resource->buf)
                    || matcher(resource, policy_resource->buf)) {
                    return GGL_ERR_OK;
                }
            }
            return GGL_ERR_FAILURE;
        }
    }

    return GGL_ERR_NOENTRY;
}

GglError ggl_ipc_auth(
    const GglIpcOperationInfo *info,
    GglBuffer resource,
    GglIpcPolicyResourceMatcher *matcher
) {
    assert(info != NULL);

    static uint8_t policy_mem[4096];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(policy_mem));

    GglObject policies;
    GglError ret = ggl_gg_config_read(
        GGL_BUF_LIST(
            GGL_STR("services"),
            info->component,
            GGL_STR("configuration"),
            GGL_STR("accessControl"),
            info->service
        ),
        &balloc.alloc,
        &policies
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to get policies for service %.*s in component %.*s.",
            (int) info->service.len,
            info->service.data,
            (int) info->component.len,
            info->component.data
        );
        return ret;
    }

    if (policies.type != GGL_TYPE_MAP) {
        GGL_LOGE("Configuration's accessControl is not a map.");
        return GGL_ERR_CONFIG;
    }

    GglMap policy_map = policies.map;

    GGL_MAP_FOREACH(policy_kv, policy_map) {
        GglObject policy = policy_kv->val;
        if (policy.type != GGL_TYPE_MAP) {
            GGL_LOGE("Policy value is not a map.");
            return GGL_ERR_CONFIG;
        }

        ret = policy_match(policy.map, info->operation, resource, matcher);
        if (ret == GGL_ERR_OK) {
            return GGL_ERR_OK;
        }
    }

    return GGL_ERR_NOENTRY;
}

bool ggl_ipc_default_policy_matcher(
    GglBuffer request_resource, GglBuffer policy_resource
) {
    GglBuffer pattern = policy_resource;
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

    GglBuffer remaining = request_resource;
    size_t start = 0;
    for (size_t i = 0; i < pattern.len; i++) {
        if (pattern.data[i] == (uint8_t) '\0') {
            GglBuffer segment = ggl_buffer_substr(pattern, start, i);
            bool match;
            size_t match_start = 0;
            if (start == 0) {
                match = ggl_buffer_has_prefix(remaining, segment);
            } else {
                match = ggl_buffer_contains(remaining, segment, &match_start);
            }
            if (!match) {
                return false;
            }
            remaining = ggl_buffer_substr(
                remaining, match_start + segment.len, SIZE_MAX
            );
            start = i + 1;
        }
    }

    if (start == 0) {
        return ggl_buffer_eq(remaining, pattern);
    }
    GglBuffer segment = ggl_buffer_substr(pattern, start, SIZE_MAX);
    return ggl_buffer_has_suffix(remaining, segment);
}
