// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "file_operation.h"
#include "ggl/recipe2unit.h"
#include "unit_file_generator.h"
#include "validate_args.h"
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_RECIPE_BUF_SIZE 256000
#define MAX_UNIT_FILE_BUF_SIZE 2048
#define MAX_COMPONENT_FILE_NAME 1024

static const char COMPONENT_NAME[] = "recipe2unit";

GglError get_recipe_obj(
    Recipe2UnitArgs *args, GglBumpAlloc *balloc, GglObject *recipe_obj
) {
    GglError ret = validate_args(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer recipe_str_buf;
    ret = open_file(args->recipe_path, &recipe_str_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = deserialize_file_content(
        args->recipe_path, recipe_str_buf, &balloc->alloc, recipe_obj
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (recipe_obj->type != GGL_TYPE_MAP) {
        GGL_LOGE(COMPONENT_NAME, "Invalid recipe format provided");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

GglError convert_to_unit(Recipe2UnitArgs *args) {
    GglError ret;
    GglObject recipe_obj;
    static uint8_t big_buffer_for_bump[MAX_RECIPE_BUF_SIZE];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));

    ret = get_recipe_obj(args, &balloc, &recipe_obj);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];
    GglBuffer response_buffer = (GglBuffer
    ) { .data = (uint8_t *) unit_file_buffer, .len = MAX_UNIT_FILE_BUF_SIZE };

    GglObject *component_name = NULL;

    ret = generate_systemd_unit(
        recipe_obj.map, &response_buffer, args, component_name
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t file_name_array[MAX_COMPONENT_FILE_NAME];
    GglBuffer file_name_buffer = (GglBuffer
    ) { .data = (uint8_t *) file_name_array, .len = MAX_COMPONENT_FILE_NAME };

    GglByteVec file_name_vector
        = { .buf = { .data = file_name_buffer.data, .len = 0 },
            .capacity = file_name_buffer.len };

    ret = ggl_byte_vec_append(&file_name_vector, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &file_name_vector, component_name->buf);
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR(".service\0"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    FILE *f = fopen((const char *) file_name_vector.buf.data, "wb");
    fwrite(response_buffer.data, sizeof(char), response_buffer.len, f);
    fclose(f);

    return GGL_ERR_OK;
}
