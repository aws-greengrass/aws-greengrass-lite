// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "ggl/recipe2unit.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

static void ggl_string_to_lower(GglBuffer object_object_to_lower) {
    for (size_t key_count = 0; key_count < object_object_to_lower.len;
         key_count++) {
        if (object_object_to_lower.data[key_count] >= 'A'
            && object_object_to_lower.data[key_count] <= 'Z') {
            object_object_to_lower.data[key_count]
                = object_object_to_lower.data[key_count] + ('a' - 'A');
        }
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError dependency_parser(GglObject *dependency_obj, GglByteVec *out) {
    GglObject *val;
    if (dependency_obj->type == GGL_TYPE_MAP) {
        for (size_t count = 0; count < dependency_obj->map.len; count++) {
            if (dependency_obj->map.pairs[count].val.type == GGL_TYPE_MAP) {
                if (ggl_map_get(
                        dependency_obj->map.pairs[count].val.map,
                        GGL_STR("dependencytype"),
                        &val
                    )) {
                    if (val->type != GGL_TYPE_BUF) {
                        return GGL_ERR_PARSE;
                    }
                    ggl_string_to_lower(val->buf);

                    if (strncmp((char *) val->buf.data, "hard", val->buf.len)
                        == 0) {
                        GglError ret
                            = ggl_byte_vec_append(out, GGL_STR("After=ggl."));
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                        ret = ggl_byte_vec_append(
                            out, dependency_obj->map.pairs[count].key
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                        ret = ggl_byte_vec_append(out, GGL_STR(".service\n"));
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                    } else {
                        GglError ret
                            = ggl_byte_vec_append(out, GGL_STR("Wants=ggl."));
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                        ret = ggl_byte_vec_append(
                            out, dependency_obj->map.pairs[count].key
                        );
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                        ret = ggl_byte_vec_append(out, GGL_STR(".service\n"));
                        if (ret != GGL_ERR_OK) {
                            return ret;
                        }
                    }
                }
            }
            // TODO: deal with version, look conflictsWith
        }
    }

    return GGL_ERR_OK;
}

static GglError fill_unit_section(
    GglObject recipe_obj, GglByteVec *concat_unit_vector
) {
    GglObject *val;

    GglError ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("[Unit]\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("Description="));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (ggl_map_get(recipe_obj.map, GGL_STR("componentdescription"), &val)) {
        if (val->type != GGL_TYPE_BUF) {
            return GGL_ERR_PARSE;
        }

        ret = ggl_byte_vec_append(concat_unit_vector, val->buf);
        if (ret != GGL_ERR_OK) {
            return ret;
        }

        ret = ggl_byte_vec_append(concat_unit_vector, GGL_STR("\n"));
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }

    if (ggl_map_get(recipe_obj.map, GGL_STR("componentdependencies"), &val)) {
        if ((val->type == GGL_TYPE_MAP) || (val->type == GGL_TYPE_LIST)) {
            return dependency_parser(val, concat_unit_vector);
        }
    }

    return GGL_ERR_OK;
}

static GglError lifecycle_selection(
    GglObject *selection_obj,
    GglObject recipe_obj,
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
                == 0)) {
            GglObject *global_lifecycle;
            // Fetch the global Lifecycle object and match the
            // name with the first occurrence of selection
            if (ggl_map_get(
                    recipe_obj.map, GGL_STR("lifecycle"), &global_lifecycle
                )) {
                if (global_lifecycle->type != GGL_TYPE_MAP) {
                    return GGL_ERR_INVALID;
                }
                if (ggl_map_get(
                        global_lifecycle->map, GGL_STR("linux"), &val
                    )) {
                    if (val->type != GGL_TYPE_MAP) {
                        GGL_LOGE(
                            "recipe2unit", "Invalid Global Linux lifecycle"
                        );
                        return GGL_ERR_INVALID;
                    }
                    selected_lifecycle_object = val;
                }
            }
        }
    }
    (void) selected_lifecycle_object;
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError manifest_selection(
    GglObject manifest_obj,
    GglObject recipe_obj,
    GglObject *selected_lifecycle_object
) {
    GglObject *val;
    if (ggl_map_get(manifest_obj.map, GGL_STR("platform"), &val)) {
        if (val->type == GGL_TYPE_MAP) {
            if (ggl_map_get(val->map, GGL_STR("os"), &val)) {
                if (val->type != GGL_TYPE_BUF) {
                    GGL_LOGE("recipe2unit", "Platform OS invalid input");
                    return GGL_ERR_INVALID;
                }
                if (strncmp((char *) val->buf.data, "linux", val->buf.len) == 0
                    || strncmp((char *) val->buf.data, "*", val->buf.len)
                        == 0) {
                    if (ggl_map_get(
                            manifest_obj.map, GGL_STR("lifecycle"), &val
                        )) {
                        if (val->type != GGL_TYPE_MAP) {
                            return GGL_ERR_INVALID;
                        }
                        // if linux lifecycle is found return the object
                        selected_lifecycle_object = val;
                        (void) selected_lifecycle_object;
                    } else if (ggl_map_get(
                                   val->map, GGL_STR("selections"), &val
                               )) {
                        if (val->type != GGL_TYPE_LIST) {
                            return GGL_ERR_INVALID;
                        }
                        return lifecycle_selection(
                            val, recipe_obj, selected_lifecycle_object
                        );
                    } else {
                        GGL_LOGE(
                            "recipe2unit",
                            "Neither Lifecycle or Selection data provided"
                        );
                        return GGL_ERR_INVALID;
                    }
                } else {
                    // If the current platform isn't linux then just proceed to
                    // next and mark current cycle success
                    return GGL_ERR_OK;
                }
            }
        } else {
            return GGL_ERR_INVALID;
        }
    } else {
        GGL_LOGE("recipe2unit", "Platform not provided");
        return GGL_ERR_INVALID;
    }
    return GGL_ERR_OK;
}

static GglError fetch_script_section(
    GglObject selected_lifecycle, GglBuffer selected_phase, const bool *is_root
) {
    (void) selected_lifecycle;
    (void) selected_phase;
    (void) is_root;

    GGL_LOGI(
        "recipe2unit",
        "Install: %*.s",
        (int) selected_lifecycle.map.pairs[0].key.len,
        selected_lifecycle.map.pairs[0].key.data
    );

    return GGL_ERR_OK;
};

static GglError fill_service_section(
    GglObject recipe_obj, GglByteVec *out, Recipe2UnitArgs *args
) {
    bool is_root = false;
    GglObject *val;
    GglObject selected_lifecycle = { 0 };

    GglError ret = ggl_byte_vec_append(out, GGL_STR("[Service]\n"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_byte_vec_append(out, GGL_STR("WorkingDirectory="));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_byte_vec_append(
        out,
        ((GglBuffer) { .data = (uint8_t *) args->root_dir,
                       .len = strlen(args->root_dir) })
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_map_get(recipe_obj.map, GGL_STR("manifests"), &val)) {
        if (val->type == GGL_TYPE_LIST) {
            for (size_t platform_index = 0;
                 platform_index < recipe_obj.list.len;
                 platform_index++) {
                ret = manifest_selection(
                    val->list.items[platform_index],
                    recipe_obj,
                    &selected_lifecycle
                );
                if (ret != GGL_ERR_OK) {
                    return ret;
                }
                // If a lifecycle is successfully selected then look no futher
                if (selected_lifecycle.type == GGL_TYPE_MAP) {
                    break;
                }
            }

            fetch_script_section(
                selected_lifecycle, GGL_STR("install"), &is_root
            );

        } else {
            GGL_LOGI("recipe2unit", "Invalid Manifest within the recipe file.");
            return GGL_ERR_INVALID;
        }
    }

    return GGL_ERR_OK;
}

GglError generate_systemd_unit(
    GglObject recipe_obj, GglBuffer *unit_file_buffer, Recipe2UnitArgs *args
) {
    GglByteVec concat_unit_vector
        = { .buf = { .data = unit_file_buffer->data, .len = 0 },
            .capacity = unit_file_buffer->len };

    GglError ret = fill_unit_section(recipe_obj, &concat_unit_vector);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_byte_vec_append(&concat_unit_vector, GGL_STR("\n"));

    ret = fill_service_section(recipe_obj, &concat_unit_vector, args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    *unit_file_buffer = concat_unit_vector.buf;
    return GGL_ERR_OK;
}
