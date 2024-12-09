// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/recipe.h"
#include <sys/types.h>
#include <ctype.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <ggl/yaml_decode.h>
#include <limits.h>
#include <pthread.h>
#include <string.h>
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
    GglObject *key_object;
    if (ggl_map_get(
            lifecycle_step, GGL_STR("RequiresPrivilege"), &key_object
        )) {
        if (key_object->type != GGL_TYPE_BUF) {
            GGL_LOGE("RequiresPrivilege needs to be a (true/false) value");
            return GGL_ERR_INVALID;
        }

        // TODO: Check if 0 and 1 are valid
        if (strncmp((char *) key_object->buf.data, "true", key_object->buf.len)
            == 0) {
            *is_root = true;
        } else if (strncmp(
                       (char *) key_object->buf.data,
                       "false",
                       key_object->buf.len
                   )
                   == 0) {
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

static GglError process_script_section_as_map(
    GglMap selected_lifecycle_phase,
    bool *is_root,
    GglBuffer *out_selected_script_as_buf,
    GglMap *out_set_env_as_map,
    GglBuffer *out_timeout_value
) {
    GglObject *key_object;

    GglError ret
        = parse_requiresprivilege_section(is_root, selected_lifecycle_phase);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_map_get(selected_lifecycle_phase, GGL_STR("Script"), &key_object)) {
        if (key_object->type != GGL_TYPE_BUF) {
            GGL_LOGE("Script section needs to be string buffer");
            return GGL_ERR_INVALID;
        }
        *out_selected_script_as_buf = key_object->buf;
    } else {
        GGL_LOGE("Script is not in the map");
        return GGL_ERR_NOENTRY;
    }

    if (ggl_map_get(selected_lifecycle_phase, GGL_STR("Setenv"), &key_object)) {
        if (key_object->type != GGL_TYPE_MAP) {
            GGL_LOGE("Setenv needs to be a dictionary map");
            return GGL_ERR_INVALID;
        }
        if (out_set_env_as_map != NULL) {
            *out_set_env_as_map = key_object->map;
        }
    }
    if (ggl_map_get(
            selected_lifecycle_phase, GGL_STR("Timeout"), &key_object
        )) {
        if (key_object->type != GGL_TYPE_BUF) {
            GGL_LOGE("Timeout needs to be numeric value");
            return GGL_ERR_INVALID;
        }
        if (!is_positive_integer(key_object->buf)) {
            GGL_LOGE("Timeout needs to be numeric value");
            return GGL_ERR_INVALID;
        }
        if (out_timeout_value != NULL) {
            *out_timeout_value = key_object->buf;
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
        if (val->type == GGL_TYPE_BUF) {
            *out_selected_script_as_buf = val->buf;
        } else if (val->type == GGL_TYPE_MAP) {
            GglError ret = process_script_section_as_map(
                val->map,
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
    GglObject *selection_obj,
    GglMap recipe_map,
    GglObject *selected_lifecycle_object
) {
    GglObject *val;
    for (size_t selection_index = 0; selection_index < selection_obj->list.len;
         selection_index++) {
        if ((strncmp(
                 (char *) selection_obj->list.items[selection_index].buf.data,
                 "all",
                 selection_obj->list.items[selection_index].buf.len
             )
             == 0)
            || (strncmp(
                    (char *) selection_obj->list.items[selection_index]
                        .buf.data,
                    "linux",
                    selection_obj->list.items[selection_index].buf.len
                )
                == 0)
            || (strncmp(
                    (char *) selection_obj->list.items[selection_index]
                        .buf.data,
                    "*",
                    selection_obj->list.items[selection_index].buf.len
                )
                == 0)) {
            GglObject *global_lifecycle;
            // Fetch the global Lifecycle object and match the
            // name with the first occurrence of selection
            if (ggl_map_get(
                    recipe_map, GGL_STR("Lifecycle"), &global_lifecycle
                )) {
                if (global_lifecycle->type != GGL_TYPE_MAP) {
                    return GGL_ERR_INVALID;
                }
                if (ggl_map_get(
                        global_lifecycle->map, GGL_STR("linux"), &val
                    )) {
                    if (val->type != GGL_TYPE_MAP) {
                        GGL_LOGE("Invalid Global Linux lifecycle");
                        return GGL_ERR_INVALID;
                    }
                    *selected_lifecycle_object = *val;
                }
            }
        }
    }
    return GGL_ERR_OK;
}

static GglBuffer get_current_architecture(void) {
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
    const GglMap *manifest_map,
    GglMap recipe_map,
    GglObject **selected_lifecycle_object
) {
    GglObject *platform;
    GglObject *os;
    if (ggl_map_get(*manifest_map, GGL_STR("Platform"), &platform)) {
        if (platform->type != GGL_TYPE_MAP) {
            return GGL_ERR_INVALID;
        }

        // If OS is not provided then do nothing
        if (ggl_map_get(platform->map, GGL_STR("os"), &os)) {
            if (os->type != GGL_TYPE_BUF) {
                GGL_LOGE("Platform OS is invalid. It must be a string");
                return GGL_ERR_INVALID;
            }

            GglObject *architecture_obj = { 0 };
            // fetch architecture_obj
            if (ggl_map_get(
                    platform->map, GGL_STR("architecture"), &architecture_obj
                )) {
                if (architecture_obj->type != GGL_TYPE_BUF) {
                    GGL_LOGE(
                        "Platform architecture is invalid. It must be a string"
                    );
                    return GGL_ERR_INVALID;
                }
            }

            GglBuffer curr_arch = get_current_architecture();

            // Check if the current OS supported first
            if ((strncmp((char *) os->buf.data, "linux", os->buf.len) == 0)
                || (strncmp((char *) os->buf.data, "*", os->buf.len) == 0)
                || (strncmp((char *) os->buf.data, "all", os->buf.len) == 0)) {
                // Then check if architecture is also supported
                if (((architecture_obj == NULL)
                     || (architecture_obj->buf.len == 0)
                     || (strncmp(
                             (char *) architecture_obj->buf.data,
                             (char *) curr_arch.data,
                             architecture_obj->buf.len
                         )
                         == 0))) {
                    GglObject *selections;
                    if (ggl_map_get(
                            *manifest_map,
                            GGL_STR("Lifecycle"),
                            selected_lifecycle_object
                        )) {
                        if ((*selected_lifecycle_object)->type
                            != GGL_TYPE_MAP) {
                            GGL_LOGE("Lifecycle object is not MAP type.");
                            return GGL_ERR_INVALID;
                        }
                    } else if (ggl_map_get(
                                   *manifest_map,
                                   GGL_STR("Selections"),
                                   &selections
                               )) {
                        if (selections->type != GGL_TYPE_LIST) {
                            return GGL_ERR_INVALID;
                        }
                        return lifecycle_selection(
                            selections, recipe_map, *selected_lifecycle_object
                        );
                    } else {
                        GGL_LOGE("Neither Lifecycle nor Selection data provided"
                        );
                        return GGL_ERR_INVALID;
                    }
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
        if (val->type != GGL_TYPE_LIST) {
            GGL_LOGI("Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGI("No Manifest found in the recipe");
        return GGL_ERR_INVALID;
    }

    GglObject *selected_lifecycle_object = NULL;
    for (size_t platform_index = 0; platform_index < val->list.len;
         platform_index++) {
        if (val->list.items[platform_index].type != GGL_TYPE_MAP) {
            GGL_LOGE("Provided manifest section is in invalid format.");
            return GGL_ERR_INVALID;
        }
        GglError ret = manifest_selection(
            &val->list.items[platform_index].map,
            recipe_map,
            &selected_lifecycle_object
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object != NULL) {
            // If a lifecycle is successfully selected then look no futher
            if (selected_lifecycle_object->type == GGL_TYPE_MAP) {
                break;
            }
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (selected_lifecycle_object->type != GGL_TYPE_MAP)) {
        GGL_LOGE("No lifecycle was found for linux");
        return GGL_ERR_FAILURE;
    }

    *out_selected_lifecycle_map = selected_lifecycle_object->map;

    return GGL_ERR_OK;
}

GglError select_linux_manifest(
    GglMap recipe_map, GglMap *out_selected_linux_manifest
) {
    GglObject *val;
    if (ggl_map_get(recipe_map, GGL_STR("Manifests"), &val)) {
        if (val->type != GGL_TYPE_LIST) {
            GGL_LOGI("Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGI("No Manifest found in the recipe");
        return GGL_ERR_INVALID;
    }

    GglObject *selected_lifecycle_object = NULL;
    for (size_t platform_index = 0; platform_index < val->list.len;
         platform_index++) {
        if (val->list.items[platform_index].type != GGL_TYPE_MAP) {
            GGL_LOGE("Provided manifest section is in invalid format.");
            return GGL_ERR_INVALID;
        }
        GglError ret = manifest_selection(
            &val->list.items[platform_index].map,
            recipe_map,
            &selected_lifecycle_object
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        if (selected_lifecycle_object != NULL) {
            // If a lifecycle is successfully selected then look no futher
            // If the lifecycle is found then the manifest will also be the same
            if (selected_lifecycle_object->type == GGL_TYPE_MAP) {
                *out_selected_linux_manifest
                    = val->list.items[platform_index].map;
                break;
            }
        }
    }

    if ((selected_lifecycle_object == NULL)
        || (selected_lifecycle_object->type != GGL_TYPE_MAP)) {
        GGL_LOGE("No Manifest was found for linux");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglError ggl_recipe_get_from_file(
    int root_path_fd,
    GglBuffer component_name,
    GglBuffer component_version,
    GglAlloc *alloc,
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
        ret = ggl_json_decode_destructive(content, alloc, recipe);
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
                return ret;
            }
        }

        ret = ggl_yaml_decode_destructive(content, alloc, recipe);
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    return ggl_obj_buffer_copy(recipe, alloc);
}
