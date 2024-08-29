// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "ggl/recipe2unit.h"
#include <ggl/error.h>
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
            dependency_parser(val, concat_unit_vector);
        }
    }

    return GGL_ERR_OK;
}

static GglError fill_service_section(
    GglObject recipe_obj, GglByteVec *out, Recipe2UnitArgs *args
) {
    bool is_root = false;
    (void) is_root;
    (void) recipe_obj;

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
