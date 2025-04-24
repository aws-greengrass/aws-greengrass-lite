// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/recipe.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <ggl/yaml_decode.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

static GglError try_open_extension(
    int recipe_dir, GglBuffer ext, GglByteVec name, GglBuffer *content
) {
    GglByteVec full = name;
    GglError ret = ggl_byte_vec_push(&full, '.');
    ggl_byte_vec_chain_append(&ret, &full, ext);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    return ggl_file_read_path_at(recipe_dir, full.buf, content);
}

static GglError parse_requiresprivilege_section(
    bool *is_root, GglMap lifecycle_step
) {
    GglObject *value_obj;
    if (ggl_map_get(lifecycle_step, GGL_STR("RequiresPrivilege"), &value_obj)) {
        if (ggl_obj_type(*value_obj) != GGL_TYPE_BUF) {
            GGL_LOGE("RequiresPrivilege needs to be a (true/false) value");
            return GGL_ERR_INVALID;
        }

        GglBuffer value = ggl_obj_into_buf(*value_obj);

        // TODO: Check if 0 and 1 are valid
        if (ggl_buffer_eq(value, GGL_STR("true"))) {
            *is_root = true;
        } else if (ggl_buffer_eq(value, GGL_STR("false"))) {
            *is_root = false;
        } else {
            GGL_LOGE("RequiresPrivilege needs to be a"
                     "(true/false) value");
            return GGL_ERR_INVALID;
        }
    }
    return GGL_ERR_OK;
}

static bool is_positive_integer(GglBuffer str) {
    // Check for null or empty string
    if (str.len == 0) {
        return false;
    }

    for (size_t counter = 0; counter < str.len; counter++) {
        // Ensure all characters are digits
        if (!isdigit(str.data[counter])) {
            return false;
        }
    }

    return true; // All characters are digits
}

bool ggl_is_recipe_variable(GglBuffer str) {
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

GglError ggl_parse_recipe_variable(
    GglBuffer str, GglRecipeVariable *out_variable
) {
    if (!ggl_is_recipe_variable(str)) {
        return GGL_ERR_INVALID;
    }
    str = ggl_buffer_substr(str, 1, str.len - 1);
    GglBufVec split = GGL_BUF_VEC((GglBuffer[3]) { 0 });
    while (str.len > 0) {
        size_t idx = 0;
        for (; idx < str.len; ++idx) {
            if (str.data[idx] == ':') {
                break;
            }
        }
        GglBuffer token = ggl_buffer_substr(str, 0, idx);
        str = ggl_buffer_substr(str, idx + 1, SIZE_MAX);
        if (token.len == 0) {
            return GGL_ERR_PARSE;
        }
        GglError ret = ggl_buf_vec_push(&split, token);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    switch (split.buf_list.len) {
    case 2:
        out_variable->type = split.buf_list.bufs[0];
        out_variable->key = split.buf_list.bufs[1];
        return GGL_ERR_OK;
    case 3:
        out_variable->component_dependency_name = split.buf_list.bufs[0];
        out_variable->type = split.buf_list.bufs[1];
        out_variable->key = split.buf_list.bufs[2];
        return GGL_ERR_OK;
    default:
        assert(false);
        return GGL_ERR_PARSE;
    }
}

static GglError process_script_section_as_map(
    GglMap selected_lifecycle_phase,
    bool *is_root,
    GglBuffer *out_selected_script_as_buf,
    GglMap *out_set_env_as_map,
    GglBuffer *out_timeout_value
) {
    GglError ret
        = parse_requiresprivilege_section(is_root, selected_lifecycle_phase);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglObject *val;
    if (ggl_map_get(selected_lifecycle_phase, GGL_STR("Script"), &val)) {
        if (ggl_obj_type(*val) != GGL_TYPE_BUF) {
            GGL_LOGE("Script section needs to be a string.");
            return GGL_ERR_INVALID;
        }
        *out_selected_script_as_buf = ggl_obj_into_buf(*val);
    } else {
        GGL_LOGE("Script is not in the map");
        return GGL_ERR_NOENTRY;
    }

    if (ggl_map_get(selected_lifecycle_phase, GGL_STR("Setenv"), &val)) {
        if (ggl_obj_type(*val) != GGL_TYPE_MAP) {
            GGL_LOGE("Setenv needs to be a map.");
            return GGL_ERR_INVALID;
        }
        if (out_set_env_as_map != NULL) {
            *out_set_env_as_map = ggl_obj_into_map(*val);
        }
    }

    if (ggl_map_get(selected_lifecycle_phase, GGL_STR("Timeout"), &val)) {
        if (ggl_obj_type(*val) != GGL_TYPE_BUF) {
            GGL_LOGE("Timeout must expand to a positive integer value");
            return GGL_ERR_INVALID;
        }
        GglBuffer timeout_str = ggl_obj_into_buf(*val);
        if (!ggl_is_recipe_variable(timeout_str)
            && !is_positive_integer(timeout_str)) {
            GGL_LOGE("Timeout must expand to a positive integer value");
            return GGL_ERR_INVALID;
        }
        if (out_timeout_value != NULL) {
            *out_timeout_value = timeout_str;
        }
    }

    return GGL_ERR_OK;
}

GglError fetch_script_section(
    GglMap selected_lifecycle,
    GglBuffer selected_phase,
    bool *is_root,
    GglBuffer *out_selected_script_as_buf,
    GglMap *out_set_env_as_map,
    GglBuffer *out_timeout_value
) {
    GglObject *val;
    if (ggl_map_get(selected_lifecycle, selected_phase, &val)) {
        if (ggl_obj_type(*val) == GGL_TYPE_BUF) {
            *out_selected_script_as_buf = ggl_obj_into_buf(*val);
        } else if (ggl_obj_type(*val) == GGL_TYPE_MAP) {
            GglError ret = process_script_section_as_map(
                ggl_obj_into_map(*val),
                is_root,
                out_selected_script_as_buf,
                out_set_env_as_map,
                out_timeout_value
            );
            if (ret != GGL_ERR_OK) {
                return ret;
            }

        } else {
            GGL_LOGE("Script section section is of invalid list type");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGW(
            "%.*s section is not in the lifecycle",
            (int) selected_phase.len,
            selected_phase.data
        );
        return GGL_ERR_NOENTRY;
    }

    return GGL_ERR_OK;
};

static GglError lifecycle_selection(
    GglList selection, GglMap recipe_map, GglObject **selected_lifecycle_object
) {
    assert(ggl_list_type_check(selection, GGL_TYPE_BUF));
    GGL_LIST_FOREACH(i, selection) {
        GglBuffer elem = ggl_obj_into_buf(*i);
        if (ggl_buffer_eq(elem, GGL_STR("all"))
            || ggl_buffer_eq(elem, GGL_STR("linux"))) {
            GglObject *global_lifecycle;
            // Fetch the global Lifecycle object and match the
            // name with the first occurrence of selection
            if (ggl_map_get(
                    recipe_map, GGL_STR("Lifecycle"), &global_lifecycle
                )) {
                if (ggl_obj_type(*global_lifecycle) != GGL_TYPE_MAP) {
                    return GGL_ERR_INVALID;
                }

                GglObject *val;
                if (ggl_map_get(
                        ggl_obj_into_map(*global_lifecycle), elem, &val
                    )) {
                    if (ggl_obj_type(*val) != GGL_TYPE_MAP) {
                        GGL_LOGE("Invalid Global Linux lifecycle");
                        return GGL_ERR_INVALID;
                    }
                    *selected_lifecycle_object = val;
                }
            }
        }
    }
    return GGL_ERR_OK;
}

GglBuffer get_current_architecture(void) {
    GglBuffer current_arch = { 0 };
#if defined(__x86_64__)
    current_arch = GGL_STR("amd64");
#elif defined(__i386__)
    current_arch = GGL_STR("x86");
#elif defined(__aarch64__)
    current_arch = GGL_STR("aarch64");
#elif defined(__arm__)
    current_arch = GGL_STR("arm");
#endif
    return current_arch;
}

// TODO: Refactor it
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError manifest_selection(
    GglMap manifest_map,
    GglMap recipe_map,
    GglObject **selected_lifecycle_object
) {
    GglObject *platform_obj;
    if (ggl_map_get(manifest_map, GGL_STR("Platform"), &platform_obj)) {
        if (ggl_obj_type(*platform_obj) != GGL_TYPE_MAP) {
            return GGL_ERR_INVALID;
        }

        GglMap platform = ggl_obj_into_map(*platform_obj);

        // If OS is not provided then do nothing
        GglObject *os_obj;
        if (ggl_map_get(platform, GGL_STR("os"), &os_obj)) {
            if (ggl_obj_type(*os_obj) != GGL_TYPE_BUF) {
                GGL_LOGE("Platform OS is invalid. It must be a string");
                return GGL_ERR_INVALID;
            }

            GglBuffer os = ggl_obj_into_buf(*os_obj);

            GglObject *architecture_obj = NULL;
            // fetch architecture_obj
            if (ggl_map_get(
                    platform, GGL_STR("architecture"), &architecture_obj
                )) {
                if (ggl_obj_type(*architecture_obj) != GGL_TYPE_BUF) {
                    GGL_LOGE(
                        "Platform architecture is invalid. It must be a string"
                    );
                    return GGL_ERR_INVALID;
                }
            }

            GglBuffer architecture = { 0 };

            if (architecture_obj != NULL) {
                architecture = ggl_obj_into_buf(*architecture_obj);
            }

            GglBuffer curr_arch = get_current_architecture();

            // Check if the current OS supported first
            if (ggl_buffer_eq(os, GGL_STR("linux"))
                || ggl_buffer_eq(os, GGL_STR("*"))
                || ggl_buffer_eq(os, GGL_STR("all"))) {
                // Then check if architecture is also supported
                if (((architecture.len == 0)
                     || ggl_buffer_eq(architecture, GGL_STR("*"))
                     || ggl_buffer_eq(architecture, curr_arch))) {
                    if (ggl_map_get(
                            manifest_map,
                            GGL_STR("Lifecycle"),
                            selected_lifecycle_object
                        )) {
                        if (ggl_obj_type(**selected_lifecycle_object)
                            != GGL_TYPE_MAP) {
                            GGL_LOGE("Lifecycle object is not a map.");
                            return GGL_ERR_INVALID;
                        }
                        // Lifecycle keyword might be there but only return
                        // if there is something inside the list
                        if (ggl_obj_into_map(**selected_lifecycle_object).len
                            != 0) {
                            return GGL_ERR_OK;
                        }
                    }

                    GglObject *selections_obj;
                    if (ggl_map_get(
                            manifest_map, GGL_STR("Selections"), &selections_obj
                        )) {
                        if (ggl_obj_type(*selections_obj) != GGL_TYPE_LIST) {
                            return GGL_ERR_INVALID;
                        }
                        GglList selections = ggl_obj_into_list(*selections_obj);
                        if (selections.len != 0) {
                            return lifecycle_selection(
                                selections,
                                recipe_map,
                                selected_lifecycle_object
                            );
                        }
                    }

                    GglList selection_default
                        = GGL_LIST(ggl_obj_buf(GGL_STR("all")));
                    return lifecycle_selection(
                        selection_default, recipe_map, selected_lifecycle_object
                    );
                }

            } else {
                // If the current platform isn't linux then just proceed to
                // next and mark current cycle success
                return GGL_ERR_OK;
            }
        }
    } else {
        GGL_LOGE("Platform not provided");
        return GGL_ERR_INVALID;
    }
    return GGL_ERR_OK;
}

GglError select_linux_lifecycle(
    GglMap recipe_map, GglMap *out_selected_lifecycle_map
) {
    GglObject *val;
    if (ggl_map_get(recipe_map, GGL_STR("Manifests"), &val)) {
        if (ggl_obj_type(*val) != GGL_TYPE_LIST) {
            GGL_LOGI("Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGI("No Manifest found in the recipe");
        return GGL_ERR_INVALID;
    }
    GglList manifests = ggl_obj_into_list(*val);

    GglObject *selected_lifecycle_object = NULL;
    GGL_LIST_FOREACH(elem, manifests) {
        if (ggl_obj_type(*elem) != GGL_TYPE_MAP) {
            GGL_LOGE("Provided manifest section is in invalid format.");
            return GGL_ERR_INVALID;
        }
        GglMap elem_map = ggl_obj_into_map(*elem);
        GglError ret = manifest_selection(
            elem_map, recipe_map, &selected_lifecycle_object
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object != NULL) {
            // If a lifecycle is successfully selected then look no futher
            if (ggl_obj_type(*selected_lifecycle_object) == GGL_TYPE_MAP) {
                break;
            }
            selected_lifecycle_object = NULL;
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (ggl_obj_type(*selected_lifecycle_object) != GGL_TYPE_MAP)) {
        GGL_LOGE("No lifecycle was found for linux");
        return GGL_ERR_FAILURE;
    }

    *out_selected_lifecycle_map = ggl_obj_into_map(*selected_lifecycle_object);

    return GGL_ERR_OK;
}

GglError select_linux_manifest(
    GglMap recipe_map, GglMap *out_selected_linux_manifest
) {
    GglObject *val;
    if (ggl_map_get(recipe_map, GGL_STR("Manifests"), &val)) {
        if (ggl_obj_type(*val) != GGL_TYPE_LIST) {
            GGL_LOGI("Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGI("No Manifest found in the recipe");
        return GGL_ERR_INVALID;
    }
    GglList manifests = ggl_obj_into_list(*val);

    GglObject *selected_lifecycle_object = NULL;
    GGL_LIST_FOREACH(elem, manifests) {
        if (ggl_obj_type(*elem) != GGL_TYPE_MAP) {
            GGL_LOGE("Provided manifest section is in invalid format.");
            return GGL_ERR_INVALID;
        }
        GglMap elem_map = ggl_obj_into_map(*elem);
        GglError ret = manifest_selection(
            elem_map, recipe_map, &selected_lifecycle_object
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object != NULL) {
            // If a lifecycle is successfully selected then look no futher
            // If the lifecycle is found then the manifest will also be the same
            if (ggl_obj_type(*selected_lifecycle_object) == GGL_TYPE_MAP) {
                *out_selected_linux_manifest = elem_map;
                break;
            }
            selected_lifecycle_object = NULL;
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (ggl_obj_type(*selected_lifecycle_object) != GGL_TYPE_MAP)) {
        GGL_LOGE("No Manifest was found for linux");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglError ggl_recipe_get_from_file(
    int root_path_fd,
    GglBuffer component_name,
    GglBuffer component_version,
    GglArena *arena,
    GglObject *recipe
) {
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&mtx);

    int recipe_dir;
    GglError ret = ggl_dir_openat(
        root_path_fd, GGL_STR("packages/recipes"), O_PATH, false, &recipe_dir
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open recipe dir.");
        return ret;
    }

    static uint8_t file_name_mem[PATH_MAX];
    GglByteVec base_name = GGL_BYTE_VEC(file_name_mem);

    ggl_byte_vec_chain_append(&ret, &base_name, component_name);
    ggl_byte_vec_chain_push(&ret, &base_name, '-');
    ggl_byte_vec_chain_append(&ret, &base_name, component_version);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Recipe path too long.");
        return ret;
    }

    static uint8_t file_mem[8196];
    GglBuffer content = GGL_BUF(file_mem);
    ret = try_open_extension(recipe_dir, GGL_STR("json"), base_name, &content);
    if (ret == GGL_ERR_OK) {
        ret = ggl_json_decode_destructive(content, arena, recipe);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    } else {
        ret = try_open_extension(
            recipe_dir, GGL_STR("yaml"), base_name, &content
        );

        if (ret != GGL_ERR_OK) {
            ret = try_open_extension(
                recipe_dir, GGL_STR("yml"), base_name, &content
            );
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "Err %d could not open recipe file for: %.*s",
                    errno,
                    (int) base_name.buf.len,
                    base_name.buf.data
                );
                return ret;
            }
        }

        ret = ggl_yaml_decode_destructive(content, arena, recipe);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return ggl_arena_claim_obj(recipe, arena);
}
