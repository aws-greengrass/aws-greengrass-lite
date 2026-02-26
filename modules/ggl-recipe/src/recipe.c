// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/json_decode.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/recipe.h>
#include <ggl/yaml_decode.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static GgError try_open_extension(
    int recipe_dir, GgBuffer ext, GgByteVec name, GgBuffer *content
) {
    GgByteVec full = name;
    GgError ret = gg_byte_vec_push(&full, '.');
    gg_byte_vec_chain_append(&ret, &full, ext);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return gg_file_read_path_at(recipe_dir, full.buf, content);
}

static GgError parse_requiresprivilege_section(
    bool *is_root, GgMap lifecycle_step
) {
    GgObject *value_obj;
    if (gg_map_get(lifecycle_step, GG_STR("RequiresPrivilege"), &value_obj)) {
        if (gg_obj_type(*value_obj) == GG_TYPE_BOOLEAN) {
            *is_root = gg_obj_into_bool(*value_obj);
            return GG_ERR_OK;
        }

        if (gg_obj_type(*value_obj) != GG_TYPE_BUF) {
            GG_LOGE("RequiresPrivilege needs to be a (true/false) value");
            return GG_ERR_INVALID;
        }

        GgBuffer value = gg_obj_into_buf(*value_obj);

        // TODO: Check if 0 and 1 are valid
        if (gg_buffer_eq(value, GG_STR("true"))) {
            *is_root = true;
        } else if (gg_buffer_eq(value, GG_STR("false"))) {
            *is_root = false;
        } else {
            GG_LOGE("RequiresPrivilege needs to be a (true/false) value");
            return GG_ERR_INVALID;
        }
    }
    return GG_ERR_OK;
}

bool ggl_is_recipe_variable(GgBuffer str) {
    if ((str.data == NULL) || (str.len < 5)) {
        return false;
    }
    if (str.data[0] != '{') {
        return false;
    }
    if (str.data[str.len - 1] != '}') {
        return false;
    }
    size_t delimiter_count = 0;
    for (size_t i = 1; i < str.len - 1; ++i) {
        if ((str.data[i] == '{') || (str.data[i] == '}')) {
            return false;
        }
        if (str.data[i] == ':') {
            delimiter_count++;
        }
    }
    if ((delimiter_count < 1) || (delimiter_count > 2)) {
        return false;
    }
    return true;
}

GgError ggl_parse_recipe_variable(
    GgBuffer str, GglRecipeVariable *out_variable
) {
    if (!ggl_is_recipe_variable(str)) {
        return GG_ERR_INVALID;
    }
    str = gg_buffer_substr(str, 1, str.len - 1);
    GgBufVec split = GG_BUF_VEC((GgBuffer[3]) { 0 });
    while (str.len > 0) {
        size_t idx = 0;
        for (; idx < str.len; ++idx) {
            if (str.data[idx] == ':') {
                break;
            }
        }
        GgBuffer token = gg_buffer_substr(str, 0, idx);
        str = gg_buffer_substr(str, idx + 1, SIZE_MAX);
        if (token.len == 0) {
            return GG_ERR_PARSE;
        }
        GgError ret = gg_buf_vec_push(&split, token);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }
    switch (split.buf_list.len) {
    case 2:
        out_variable->type = split.buf_list.bufs[0];
        out_variable->key = split.buf_list.bufs[1];
        return GG_ERR_OK;
    case 3:
        out_variable->component_dependency_name = split.buf_list.bufs[0];
        out_variable->type = split.buf_list.bufs[1];
        out_variable->key = split.buf_list.bufs[2];
        return GG_ERR_OK;
    default:
        assert(false);
        return GG_ERR_PARSE;
    }
}

static bool parse_positive_integer(GgBuffer str, uint64_t *result) {
    if (str.len == 0) {
        return false;
    }

    size_t counter = 0;
    while (counter < str.len && isspace(str.data[counter])) {
        counter++;
    }

    *result = 0;
    for (size_t i = counter; i < str.len; i++) {
        if (!isdigit(str.data[i])) {
            return false;
        }
        uint64_t digit = str.data[i] - '0';
        if (*result > (UINT64_MAX - digit) / 10) {
            GG_LOGE("Integer overflow detected while parsing config value");
            return false;
        }
        *result = *result * 10 + digit;
    }

    return true;
}

static GgError process_script_section_as_map(
    GgMap selected_lifecycle_phase,
    bool *is_root,
    GgBuffer *out_selected_script_as_buf,
    GgMap *out_set_env_as_map,
    GgBuffer *out_timeout_value
) {
    GgError ret
        = parse_requiresprivilege_section(is_root, selected_lifecycle_phase);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgObject *val;
    if (gg_map_get(selected_lifecycle_phase, GG_STR("Script"), &val)) {
        if (gg_obj_type(*val) != GG_TYPE_BUF) {
            GG_LOGE("Script section needs to be a string.");
            return GG_ERR_INVALID;
        }
        *out_selected_script_as_buf = gg_obj_into_buf(*val);
    } else {
        GG_LOGE("Script is not in the map");
        return GG_ERR_NOENTRY;
    }

    if (gg_map_get(selected_lifecycle_phase, GG_STR("Setenv"), &val)) {
        if (gg_obj_type(*val) != GG_TYPE_MAP) {
            GG_LOGE("Setenv needs to be a map.");
            return GG_ERR_INVALID;
        }
        if (out_set_env_as_map != NULL) {
            *out_set_env_as_map = gg_obj_into_map(*val);
        }
    }

    if (gg_map_get(selected_lifecycle_phase, GG_STR("Timeout"), &val)) {
        int64_t timeout_i64 = 0;

        if (gg_obj_type(*val) == GG_TYPE_I64) {
            timeout_i64 = gg_obj_into_i64(*val);
        } else if (gg_obj_type(*val) == GG_TYPE_BUF) {
            if (!parse_positive_integer(
                    gg_obj_into_buf(*val), (uint64_t *) &timeout_i64
                )) {
                GG_LOGE("Timeout must expand to a positive integer value");
                return GG_ERR_INVALID;
            }
        } else {
            GG_LOGE("Timeout must expand to a positive integer value");
            return GG_ERR_INVALID;
        }

        if (timeout_i64 < 0) {
            GG_LOGE("Timeout must be a positive integer value");
            return GG_ERR_INVALID;
        }

        static uint8_t timeout_mem[32];
        int len = snprintf(
            (char *) timeout_mem, sizeof(timeout_mem), "%" PRId64, timeout_i64
        );

        GgBuffer timeout_buf = { .data = timeout_mem, .len = (size_t) len };
        if (out_timeout_value != NULL) {
            *out_timeout_value = timeout_buf;
        }
    }

    return GG_ERR_OK;
}

GgError fetch_script_section(
    GgMap selected_lifecycle,
    GgBuffer selected_phase,
    bool *is_root,
    GgBuffer *out_selected_script_as_buf,
    GgMap *out_set_env_as_map,
    GgBuffer *out_timeout_value
) {
    GgObject *val;
    if (gg_map_get(selected_lifecycle, selected_phase, &val)) {
        if (gg_obj_type(*val) == GG_TYPE_BUF) {
            *out_selected_script_as_buf = gg_obj_into_buf(*val);
        } else if (gg_obj_type(*val) == GG_TYPE_MAP) {
            GgError ret = process_script_section_as_map(
                gg_obj_into_map(*val),
                is_root,
                out_selected_script_as_buf,
                out_set_env_as_map,
                out_timeout_value
            );
            if (ret != GG_ERR_OK) {
                return ret;
            }

        } else {
            GG_LOGE("Script section section is of invalid list type");
            return GG_ERR_INVALID;
        }
    } else {
        GG_LOGW(
            "%.*s section is not in the lifecycle",
            (int) selected_phase.len,
            selected_phase.data
        );
        return GG_ERR_NOENTRY;
    }

    return GG_ERR_OK;
};

static GgError lifecycle_selection(
    GgList selection, GgMap recipe_map, GgObject **selected_lifecycle_object
) {
    GgError ret = gg_list_type_check(selection, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Selection is not a list of buffers");
        return ret;
    }
    GG_LIST_FOREACH (i, selection) {
        GgBuffer elem = gg_obj_into_buf(*i);
        if (gg_buffer_eq(elem, GG_STR("all"))
            || gg_buffer_eq(elem, GG_STR("linux"))) {
            GgObject *global_lifecycle;
            // Fetch the global Lifecycle object and match the
            // name with the first occurrence of selection
            if (gg_map_get(
                    recipe_map, GG_STR("Lifecycle"), &global_lifecycle
                )) {
                if (gg_obj_type(*global_lifecycle) != GG_TYPE_MAP) {
                    return GG_ERR_INVALID;
                }

                GgObject *val;
                if (gg_map_get(
                        gg_obj_into_map(*global_lifecycle), elem, &val
                    )) {
                    if (gg_obj_type(*val) != GG_TYPE_MAP) {
                        GG_LOGE("Invalid Global Linux lifecycle");
                        return GG_ERR_INVALID;
                    }
                    *selected_lifecycle_object = val;
                }
            }
        }
    }
    return GG_ERR_OK;
}

GgBuffer get_current_architecture(void) {
    GgBuffer current_arch = { 0 };
#if defined(__x86_64__)
    current_arch = GG_STR("amd64");
#elif defined(__i386__)
    current_arch = GG_STR("x86");
#elif defined(__aarch64__)
    current_arch = GG_STR("aarch64");
#elif defined(__arm__)
    current_arch = GG_STR("arm");
#elif defined(__riscv) && (__riscv_xlen == 64)
    current_arch = GG_STR("riscv64");
#endif
    return current_arch;
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError manifest_selection(
    GgMap manifest_map, GgMap recipe_map, GgObject **selected_lifecycle_object
) {
    GgObject *platform_obj;
    if (gg_map_get(manifest_map, GG_STR("Platform"), &platform_obj)) {
        if (gg_obj_type(*platform_obj) != GG_TYPE_MAP) {
            return GG_ERR_INVALID;
        }

        GgMap platform = gg_obj_into_map(*platform_obj);

        // The manifest is only valid for lite if runtime is explicitly
        // aws_nucleus_lite or *. If the value isn't set then that manifest is
        // classic-only.
        GgObject *runtime_obj = NULL;
        if (gg_map_get(platform, GG_STR("runtime"), &runtime_obj)) {
            if (gg_obj_type(*runtime_obj) != GG_TYPE_BUF) {
                GG_LOGE("Platform runtime is invalid. It must be a string");
                return GG_ERR_INVALID;
            }

            GgBuffer runtime_str = gg_obj_into_buf(*runtime_obj);
            if (!gg_buffer_eq(runtime_str, GG_STR("*"))
                && !gg_buffer_eq(runtime_str, GG_STR("aws_nucleus_lite"))) {
                GG_LOGD("Skipping manifest as it is not for aws_nucleus_lite");
                return GG_ERR_OK;
            }
        } else {
            // If runtime field is not set, that explicitly means classic-only
            GG_LOGD(
                "Skipping manifest as it does not include a runtime platform field."
            );
            return GG_ERR_OK;
        }

        // If OS is not provided then do nothing
        GgObject *os_obj;
        if (gg_map_get(platform, GG_STR("os"), &os_obj)) {
            if (gg_obj_type(*os_obj) != GG_TYPE_BUF) {
                GG_LOGE("Platform OS is invalid. It must be a string");
                return GG_ERR_INVALID;
            }

            GgBuffer os = gg_obj_into_buf(*os_obj);

            GgObject *architecture_obj = NULL;
            // fetch architecture_obj
            if (gg_map_get(
                    platform, GG_STR("architecture"), &architecture_obj
                )) {
                if (gg_obj_type(*architecture_obj) != GG_TYPE_BUF) {
                    GG_LOGE(
                        "Platform architecture is invalid. It must be a string"
                    );
                    return GG_ERR_INVALID;
                }
            }

            GgBuffer architecture = { 0 };

            if (architecture_obj != NULL) {
                architecture = gg_obj_into_buf(*architecture_obj);
            }

            GgBuffer curr_arch = get_current_architecture();

            // Check if the current OS supported first
            if (gg_buffer_eq(os, GG_STR("linux"))
                || gg_buffer_eq(os, GG_STR("*"))
                || gg_buffer_eq(os, GG_STR("all"))) {
                // Then check if architecture is also supported
                if (((architecture.len == 0)
                     || gg_buffer_eq(architecture, GG_STR("*"))
                     || gg_buffer_eq(architecture, curr_arch))) {
                    if (gg_map_get(
                            manifest_map,
                            GG_STR("Lifecycle"),
                            selected_lifecycle_object
                        )) {
                        if (gg_obj_type(**selected_lifecycle_object)
                            != GG_TYPE_MAP) {
                            GG_LOGE("Lifecycle object is not a map.");
                            return GG_ERR_INVALID;
                        }
                        // Lifecycle keyword might be there but only return
                        // if there is something inside the list
                        if (gg_obj_into_map(**selected_lifecycle_object).len
                            != 0) {
                            return GG_ERR_OK;
                        }
                    }

                    GgObject *selections_obj;
                    if (gg_map_get(
                            manifest_map, GG_STR("Selections"), &selections_obj
                        )) {
                        if (gg_obj_type(*selections_obj) != GG_TYPE_LIST) {
                            return GG_ERR_INVALID;
                        }
                        GgList selections = gg_obj_into_list(*selections_obj);
                        if (selections.len != 0) {
                            return lifecycle_selection(
                                selections,
                                recipe_map,
                                selected_lifecycle_object
                            );
                        }
                    }

                    GgList selection_default
                        = GG_LIST(gg_obj_buf(GG_STR("all")));
                    return lifecycle_selection(
                        selection_default, recipe_map, selected_lifecycle_object
                    );
                }

            } else {
                // If the current platform isn't linux then just proceed to
                // next and mark current cycle success
                return GG_ERR_OK;
            }
        }
    } else {
        GG_LOGE("Platform not provided");
        return GG_ERR_INVALID;
    }
    return GG_ERR_OK;
}

GgError select_linux_lifecycle(
    GgMap recipe_map, GgMap *out_selected_lifecycle_map
) {
    GgObject *val;
    if (gg_map_get(recipe_map, GG_STR("Manifests"), &val)) {
        if (gg_obj_type(*val) != GG_TYPE_LIST) {
            GG_LOGI("Invalid Manifest within the recipe file.");
            return GG_ERR_INVALID;
        }
    } else {
        GG_LOGI("No Manifest found in the recipe");
        return GG_ERR_INVALID;
    }
    GgList manifests = gg_obj_into_list(*val);

    GgObject *selected_lifecycle_object = NULL;
    GG_LIST_FOREACH (elem, manifests) {
        if (gg_obj_type(*elem) != GG_TYPE_MAP) {
            GG_LOGE("Provided manifest section is in invalid format.");
            return GG_ERR_INVALID;
        }
        GgMap elem_map = gg_obj_into_map(*elem);
        GgError ret = manifest_selection(
            elem_map, recipe_map, &selected_lifecycle_object
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object != NULL) {
            // If a lifecycle is successfully selected then look no futher
            if (gg_obj_type(*selected_lifecycle_object) == GG_TYPE_MAP) {
                break;
            }
            selected_lifecycle_object = NULL;
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (gg_obj_type(*selected_lifecycle_object) != GG_TYPE_MAP)) {
        GG_LOGE("No lifecycle was found for linux");
        return GG_ERR_FAILURE;
    }

    *out_selected_lifecycle_map = gg_obj_into_map(*selected_lifecycle_object);

    return GG_ERR_OK;
}

GgError ggl_get_recipe_artifacts_for_platform(
    GgMap recipe_map, GgList *out_platform_artifacts
) {
    GgMap linux_manifest = { 0 };
    GgError ret = select_linux_manifest(recipe_map, &linux_manifest);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GgObject *artifact_list = NULL;
    if (!gg_map_get(linux_manifest, GG_STR("Artifacts"), &artifact_list)) {
        GG_LOGE("Missing required 'Artifacts' key in Manifest");
        return GG_ERR_PARSE;
    }
    if (gg_obj_type(*artifact_list) != GG_TYPE_LIST) {
        GG_LOGE("Artifacts was not a list");
        return GG_ERR_PARSE;
    }
    GgList artifacts = gg_obj_into_list(*artifact_list);
    if (gg_list_type_check(artifacts, GG_TYPE_MAP)) {
        GG_LOGE("Artifacts was not a list of maps.");
        return GG_ERR_PARSE;
    }
    *out_platform_artifacts = artifacts;
    return GG_ERR_OK;
}

GgError select_linux_manifest(
    GgMap recipe_map, GgMap *out_selected_linux_manifest
) {
    GgObject *val;
    if (gg_map_get(recipe_map, GG_STR("Manifests"), &val)) {
        if (gg_obj_type(*val) != GG_TYPE_LIST) {
            GG_LOGI("Invalid Manifest within the recipe file.");
            return GG_ERR_INVALID;
        }
    } else {
        GG_LOGI("No Manifest found in the recipe");
        return GG_ERR_INVALID;
    }
    GgList manifests = gg_obj_into_list(*val);

    GgObject *selected_lifecycle_object = NULL;
    GG_LIST_FOREACH (elem, manifests) {
        if (gg_obj_type(*elem) != GG_TYPE_MAP) {
            GG_LOGE("Provided manifest section is in invalid format.");
            return GG_ERR_INVALID;
        }
        GgMap elem_map = gg_obj_into_map(*elem);
        GgError ret = manifest_selection(
            elem_map, recipe_map, &selected_lifecycle_object
        );
        if (ret != GG_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object != NULL) {
            // If a lifecycle is successfully selected then look no futher
            // If the lifecycle is found then the manifest will also be the same
            if (gg_obj_type(*selected_lifecycle_object) == GG_TYPE_MAP) {
                *out_selected_linux_manifest = elem_map;
                break;
            }
            selected_lifecycle_object = NULL;
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (gg_obj_type(*selected_lifecycle_object) != GG_TYPE_MAP)) {
        GG_LOGE("No Manifest was found for linux");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

GgError ggl_recipe_get_from_file(
    int root_path_fd,
    GgBuffer component_name,
    GgBuffer component_version,
    GgArena *arena,
    GgObject *recipe
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GG_MTX_SCOPE_GUARD(&mtx);

    int recipe_dir;
    GgError ret = gg_dir_openat(
        root_path_fd, GG_STR("packages/recipes"), O_PATH, false, &recipe_dir
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open recipe dir.");
        return ret;
    }
    GG_CLEANUP(cleanup_close, recipe_dir);

    static uint8_t file_name_mem[PATH_MAX];
    GgByteVec base_name = GG_BYTE_VEC(file_name_mem);

    gg_byte_vec_chain_append(&ret, &base_name, component_name);
    gg_byte_vec_chain_push(&ret, &base_name, '-');
    gg_byte_vec_chain_append(&ret, &base_name, component_version);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Recipe path too long.");
        return ret;
    }

    static uint8_t file_mem[8196];
    GgBuffer content = GG_BUF(file_mem);
    ret = try_open_extension(recipe_dir, GG_STR("json"), base_name, &content);
    if (ret == GG_ERR_OK) {
        ret = gg_json_decode_destructive(content, arena, recipe);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        ret = try_open_extension(
            recipe_dir, GG_STR("yaml"), base_name, &content
        );

        if (ret != GG_ERR_OK) {
            ret = try_open_extension(
                recipe_dir, GG_STR("yml"), base_name, &content
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE(
                    "Err %d could not open recipe file for: %.*s",
                    errno,
                    (int) base_name.buf.len,
                    base_name.buf.data
                );
                return ret;
            }
        }

        ret = ggl_yaml_decode_destructive(content, arena, recipe);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    }

    return gg_arena_claim_obj(recipe, arena);
}
