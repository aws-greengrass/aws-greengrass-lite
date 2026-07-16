// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_authz.h"
#include "ipc_service.h"
#include <assert.h>
#include <config_reader.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/io.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/json_pointer.h>
#include <ggl/policy_validation.h>
#include <interpolation.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GgError core_bus_config_reader_impl(
    void *ctx, GgBuffer json_ptr, GgObjectReceiver writer
) {
    GgArena *alloc = ctx;

    GgBuffer key_path_mem[GG_MAX_OBJECT_DEPTH];
    GgBufVec key_path = GG_BUF_VEC(key_path_mem);

    GgError ret = ggl_gg_config_jsonp_parse(json_ptr, &key_path);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse json pointer key.");
        return ret;
    }

    GgObject result;
    ret = ggl_gg_config_read(key_path.buf_list, alloc, &result);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to read config from core bus.");
        return ret;
    }

    return gg_object_receiver_submit(writer, result);
}

static GgConfigReader core_bus_config_receiver(GgArena *alloc) {
    return (GgConfigReader) { .ctx = alloc,
                              .reader = core_bus_config_reader_impl };
}

/// Maximum length of an interpolated policy resource string.
#define MAX_INTERPOLATED_RESOURCE_LEN 2048

/// Check if a buffer contains any {...} recipe variable sequences.
/// Skips ${...} escape sequences (those are handled by the matcher).
static bool has_recipe_variables(GgBuffer buf) {
    for (size_t i = 0; i < buf.len; i++) {
        if (buf.data[i] == (uint8_t) '{') {
            if ((i > 0) && (buf.data[i - 1] == (uint8_t) '$')) {
                continue;
            }
            return true;
        }
    }
    return false;
}

/// Interpolate {...} recipe variable sequences in a policy resource string.
/// Writes the interpolated result into vec.
/// ${...} escape sequences are passed through literally (not interpolated
/// here; the matcher handles those separately).
/// Returns GG_ERR_OK on success.
static GgError interpolate_policy_resource(
    GgBuffer policy_resource,
    GgBuffer component_name,
    GgBuffer root_path,
    GgBuffer component_version,
    GgBuffer thing_name,
    GgByteVec *vec
) {
    GgWriter writer = gg_byte_vec_writer(vec);
    size_t i = 0;

    while (i < policy_resource.len) {
        if (policy_resource.data[i] == (uint8_t) '{') {
            // ${...} is an escape sequence, not a recipe variable.
            // Pass the '$' and '{' through literally.
            if ((i > 0) && (policy_resource.data[i - 1] == (uint8_t) '$')) {
                GgError ret = gg_byte_vec_push(vec, policy_resource.data[i]);
                if (ret != GG_ERR_OK) {
                    return ret;
                }
                i++;
                continue;
            }

            // Found start of recipe variable, find the closing '}'
            size_t start = i + 1;
            size_t end = start;
            while (end < policy_resource.len
                   && policy_resource.data[end] != (uint8_t) '}') {
                end++;
            }
            if (end >= policy_resource.len) {
                GG_LOGE("Unterminated recipe variable in policy resource.");
                return GG_ERR_INVALID;
            }

            GgBuffer escape_seq = gg_buffer_substr(policy_resource, start, end);

            static uint8_t config_mem[MAX_INTERPOLATED_RESOURCE_LEN];
            GgArena alloc = gg_arena_init(GG_BUF(config_mem));

            GgError ret = ggl_substitute_escape(
                writer,
                escape_seq,
                root_path,
                component_name,
                component_version,
                thing_name,
                core_bus_config_receiver(&alloc)
            );
            if (ret != GG_ERR_OK) {
                return ret;
            }

            i = end + 1; // Skip past '}'
        } else {
            GgError ret = gg_byte_vec_push(vec, policy_resource.data[i]);
            if (ret != GG_ERR_OK) {
                return ret;
            }
            i++;
        }
    }

    return GG_ERR_OK;
}

static GgError policy_match(
    GgMap policy,
    GgBuffer operation,
    GgBuffer resource,
    GglIpcPolicyResourceMatcher *matcher,
    GgBuffer component_name,
    GgBuffer root_path,
    GgBuffer component_version,
    GgBuffer thing_name
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

                if (gg_buffer_eq(GG_STR("*"), policy_resource)) {
                    return GG_ERR_OK;
                }

                // If the policy resource contains recipe variables,
                // interpolate them before matching.
                if (has_recipe_variables(policy_resource)) {
                    static uint8_t
                        interpolated_mem[MAX_INTERPOLATED_RESOURCE_LEN];
                    GgByteVec vec = GG_BYTE_VEC(interpolated_mem);

                    ret = interpolate_policy_resource(
                        policy_resource,
                        component_name,
                        root_path,
                        component_version,
                        thing_name,
                        &vec
                    );
                    if (ret != GG_ERR_OK) {
                        GG_LOGW(
                            "Failed to interpolate policy resource, skipping."
                        );
                        continue;
                    }

                    if (matcher(resource, vec.buf)) {
                        return GG_ERR_OK;
                    }
                } else {
                    if (matcher(resource, policy_resource)) {
                        return GG_ERR_OK;
                    }
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

    GgError ret = ggl_validate_policy_resource(resource);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t policy_mem[4096];
    GgArena alloc = gg_arena_init(GG_BUF(policy_mem));

    GgObject policies;
    ret = ggl_gg_config_read(
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

    // Read system config values needed for recipe variable interpolation.
    static uint8_t root_path_mem[256];
    GgArena root_path_alloc = gg_arena_init(GG_BUF(root_path_mem));
    GgBuffer root_path = { 0 };
    (void) ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootPath")),
        &root_path_alloc,
        &root_path
    );

    static uint8_t thing_name_mem[128];
    GgArena thing_name_alloc = gg_arena_init(GG_BUF(thing_name_mem));
    GgBuffer thing_name = { 0 };
    (void) ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("thingName")),
        &thing_name_alloc,
        &thing_name
    );

    static uint8_t version_mem[64];
    GgArena version_alloc = gg_arena_init(GG_BUF(version_mem));
    GgBuffer component_version = { 0 };
    (void) ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("services"), info->component, GG_STR("version")),
        &version_alloc,
        &component_version
    );

    GgMap policy_map = gg_obj_into_map(policies);

    GG_MAP_FOREACH (policy_kv, policy_map) {
        GgObject policy = *gg_kv_val(policy_kv);
        if (gg_obj_type(policy) != GG_TYPE_MAP) {
            GG_LOGE("Policy value is not a map.");
            return GG_ERR_CONFIG;
        }

        ret = policy_match(
            gg_obj_into_map(policy),
            info->operation,
            resource,
            matcher,
            info->component,
            root_path,
            component_version,
            thing_name
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
