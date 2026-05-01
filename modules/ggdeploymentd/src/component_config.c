// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_config.h"
#include "deployment_model.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/json_decode.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/json_pointer.h>
#include <stddef.h>
#include <stdint.h>

static GgError apply_reset_config(
    GgBuffer component_name, GgMap component_config_map
) {
    GgObject *reset_configuration = NULL;
    GgError ret = gg_map_validate(
        component_config_map,
        GG_MAP_SCHEMA(
            { GG_STR("reset"), GG_OPTIONAL, GG_TYPE_LIST, &reset_configuration }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // If there is no reset configuration, then there is no
    // configuration update to make
    if (reset_configuration == NULL) {
        return GG_ERR_OK;
    }

    if (gg_obj_type(*reset_configuration) != GG_TYPE_LIST) {
        GG_LOGE(
            "Reset update did not parse into a list during configuration updates."
        );
        return GG_ERR_INVALID;
    }
    GG_LIST_FOREACH (reset_element, gg_obj_into_list(*reset_configuration)) {
        if (gg_obj_type(*reset_element) != GG_TYPE_BUF) {
            GG_LOGE(
                "Configuration key for reset config update not provided as a buffer."
            );
            return GG_ERR_INVALID;
        }

        // Empty string means they want to reset the whole configuration to
        // default configuration.
        if (gg_buffer_eq(gg_obj_into_buf(*reset_element), GG_STR(""))) {
            GG_LOGI(
                "Received a request to reset the entire configuration for %.*s",
                (int) component_name.len,
                component_name.data
            );
            ret = ggl_gg_config_delete(GG_BUF_LIST(
                GG_STR("services"), component_name, GG_STR("configuration")
            ));
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Error while deleting the component %.*s's configuration.",
                    (int) component_name.len,
                    component_name.data
                );
                return ret;
            }

            break;
        }

        static GgBuffer key_path_mem[GG_MAX_OBJECT_DEPTH];
        GgBufVec key_path = GG_BUF_VEC(key_path_mem);
        ret = gg_buf_vec_push(&key_path, GG_STR("services"));
        gg_buf_vec_chain_push(&ret, &key_path, component_name);
        gg_buf_vec_chain_push(&ret, &key_path, GG_STR("configuration"));
        if (ret != GG_ERR_OK) {
            GG_LOGE("Too many configuration levels during config reset.");
            return ret;
        }

        ret = ggl_gg_config_jsonp_parse(
            gg_obj_into_buf(*reset_element), &key_path
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Error parsing json pointer for config reset");
            return ret;
        }

        ret = ggl_gg_config_delete(key_path.buf_list);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to perform configuration reset updates for component %.*s.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }

        GG_LOGI(
            "Made a configuration reset update for component %.*s",
            (int) component_name.len,
            component_name.data
        );
    }

    return GG_ERR_OK;
}

static GgError apply_merge_config(
    GgBuffer component_name, GgMap component_config_map
) {
    GgObject *merge_configuration = NULL;
    GgError ret = gg_map_validate(
        component_config_map,
        GG_MAP_SCHEMA(
            { GG_STR("merge"), GG_OPTIONAL, GG_TYPE_MAP, &merge_configuration }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // If there is no merge configuration, then there is no
    // configuration update to make
    if (merge_configuration == NULL) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*merge_configuration) != GG_TYPE_MAP) {
        GG_LOGE(
            "Merge update did not parse into a map during configuration updates."
        );
        return GG_ERR_INVALID;
    }

    // TODO: Use deployment timestamp not the current timestamp
    // after we support deployment timestamp
    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"), component_name, GG_STR("configuration")
        ),
        *merge_configuration,
        NULL
    );

    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to write configuration merge updates for component %.*s to ggconfigd.",
            (int) component_name.len,
            component_name.data
        );
        return ret;
    }

    GG_LOGI(
        "Made a configuration merge update for component %.*s",
        (int) component_name.len,
        component_name.data
    );

    return GG_ERR_OK;
}

GgError apply_configurations(
    GglDeployment *deployment, GgBuffer component_name, GgBuffer operation
) {
    assert(
        gg_buffer_eq(operation, GG_STR("merge"))
        || gg_buffer_eq(operation, GG_STR("reset"))
    );

    GgObject *doc_component_info = NULL;
    GgError ret = gg_map_validate(
        deployment->components,
        GG_MAP_SCHEMA(
            { component_name, GG_OPTIONAL, GG_TYPE_MAP, &doc_component_info }
        )
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // No config items to write if the component is not a root component in
    // the deployment
    if (doc_component_info == NULL) {
        return GG_ERR_OK;
    }
    if (gg_obj_type(*doc_component_info) != GG_TYPE_MAP) {
        GG_LOGE(
            "Component information did not parse into a map during configuration updates."
        );
        return GG_ERR_INVALID;
    }

    GgObject *component_configuration = NULL;
    ret = gg_map_validate(
        gg_obj_into_map(*doc_component_info),
        GG_MAP_SCHEMA({ GG_STR("configurationUpdate"),
                        GG_OPTIONAL,
                        GG_TYPE_MAP,
                        &component_configuration })
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // No config items to write if there is no configurationUpdate item
    if (component_configuration == NULL) {
        return GG_ERR_OK;
    }

    if (gg_obj_type(*component_configuration) != GG_TYPE_MAP) {
        GG_LOGE(
            "Configuration update did not parse into a map during configuration updates."
        );
        return GG_ERR_INVALID;
    }

    if (gg_buffer_eq(operation, GG_STR("merge"))) {
        ret = apply_merge_config(
            component_name, gg_obj_into_map(*component_configuration)
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    if (gg_buffer_eq(operation, GG_STR("reset"))) {
        ret = apply_reset_config(
            component_name, gg_obj_into_map(*component_configuration)
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return GG_ERR_OK;
}

// Pure helper: parse a single component_to_configuration entry and return
// pointers to the merge and/or reset payloads that should be written to
// services.<component>.configuration.
//
// Three supported shapes for config_update_obj:
//   - GG_TYPE_MAP: direct merge map (Rust SDK / core-bus form). No
//                  merge/reset wrapper — whole map is the merge payload.
//                  *merge_out is set to config_update_obj; *reset_out (if
//                  non-NULL) is set to NULL.
//   - GG_TYPE_BUF: JSON-string form (CLI path). Must decode to an object.
//                  If it has a "merge" key, *merge_out points inside the
//                  decoded tree (which lives in `alloc`); else *merge_out
//                  is NULL. If it has a "reset" key and reset_out is
//                  non-NULL, *reset_out points inside the decoded tree;
//                  else *reset_out (if non-NULL) is NULL.
//   - anything else: returns GG_ERR_INVALID.
//
// `alloc` MUST outlive any use of *merge_out / *reset_out because the
// decoded JSON tree is allocated inside it for the BUF path.
//
// reset_out MAY be NULL. If NULL, reset extraction is skipped entirely —
// useful for callers that only support merge.
//
// On GG_ERR_OK with a non-NULL out-pointer == NULL: "nothing to apply" for
// that payload — the caller should skip the corresponding write.
GgError extract_merge_and_reset_payloads(
    GgObject *config_update_obj,
    GgArena *alloc,
    GgObject **merge_out,
    GgObject **reset_out
) {
    *merge_out = NULL;
    if (reset_out != NULL) {
        *reset_out = NULL;
    }

    if (gg_obj_type(*config_update_obj) == GG_TYPE_MAP) {
        // Direct-map form (Rust SDK / core-bus): whole map is the merge
        // payload. No "merge"/"reset" wrapper.
        *merge_out = config_update_obj;
        return GG_ERR_OK;
    }

    if (gg_obj_type(*config_update_obj) == GG_TYPE_BUF) {
        // JSON-string form: parse and extract "merge" and optionally
        // "reset" fields.
        GgObject parsed;
        GgError ret = gg_json_decode_destructive(
            gg_obj_into_buf(*config_update_obj), alloc, &parsed
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to parse component_to_configuration JSON.");
            return ret;
        }
        if (gg_obj_type(parsed) != GG_TYPE_MAP) {
            GG_LOGE("component_to_configuration JSON is not an object.");
            return GG_ERR_INVALID;
        }
        // merge (optional)
        GgObject *merge_obj;
        if (gg_map_get(gg_obj_into_map(parsed), GG_STR("merge"), &merge_obj)) {
            *merge_out = merge_obj;
        }
        // reset (optional)
        if (reset_out != NULL) {
            GgObject *reset_obj;
            if (gg_map_get(
                    gg_obj_into_map(parsed), GG_STR("reset"), &reset_obj
                )) {
                *reset_out = reset_obj;
            }
        }
        return GG_ERR_OK;
    }

    GG_LOGE("component_to_configuration value must be a map or JSON buffer.");
    return GG_ERR_INVALID;
}

GgError apply_component_to_configuration(
    GgBuffer component_name, GgMap component_to_configuration
) {
    GgObject *config_update_obj;
    if (!gg_map_get(
            component_to_configuration, component_name, &config_update_obj
        )) {
        return GG_ERR_OK;
    }

    static uint8_t parse_mem[4096];
    GgArena parse_alloc = gg_arena_init(GG_BUF(parse_mem));
    GgObject *merge_obj = NULL;
    GgObject *reset_obj = NULL;
    GgError ret = extract_merge_and_reset_payloads(
        config_update_obj, &parse_alloc, &merge_obj, &reset_obj
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Apply reset BEFORE merge, matching AWS IoT Greengrass Core semantics
    // (reset updates are applied before merge updates — see
    // ComponentConfigurationUpdate docs).
    if (reset_obj != NULL) {
        // apply_reset_config expects a wrapper map with a "reset" key so we
        // can reuse the schema validator.
        GgKV reset_wrapper_kv = gg_kv(GG_STR("reset"), *reset_obj);
        GgMap reset_wrapper = (GgMap) { .pairs = &reset_wrapper_kv, .len = 1 };
        ret = apply_reset_config(component_name, reset_wrapper);
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to apply reset for %.*s from componentToConfiguration.",
                (int) component_name.len,
                component_name.data
            );
            return ret;
        }
    }

    if (merge_obj == NULL) {
        // No merge payload (e.g. reset-only or empty wrapper). Done.
        return GG_ERR_OK;
    }

    ret = ggl_gg_config_write(
        GG_BUF_LIST(
            GG_STR("services"), component_name, GG_STR("configuration")
        ),
        *merge_obj,
        &(int64_t) { 0 }
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to merge component configuration.");
        return ret;
    }
    GG_LOGI(
        "Applied configuration merge for %.*s.",
        (int) component_name.len,
        component_name.data
    );

    return GG_ERR_OK;
}
