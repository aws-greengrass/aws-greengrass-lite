// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/recipe2unit.h"
#include "unit_file_generator.h"
#include "validate_args.h"
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_UNIT_FILE_BUF_SIZE 2048
#define MAX_COMPONENT_FILE_NAME 1024

GglError convert_to_unit(
    Recipe2UnitArgs *args,
    GglAlloc *alloc,
    GglObject *recipe_obj,
    GglObject **component_name
) {
    GglError ret;
    *component_name = NULL;

    ret = validate_args(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ret = ggl_recipe_get_from_file(
        args->root_path_fd,
        args->component_name,
        args->component_version,
        alloc,
        recipe_obj
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];
    GglBuffer response_buffer = (GglBuffer
    ) { .data = (uint8_t *) unit_file_buffer, .len = MAX_UNIT_FILE_BUF_SIZE };

    ret = generate_systemd_unit(
        &recipe_obj->map, &response_buffer, args, component_name
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (*component_name == NULL) {
        GGL_LOGE("Component name was NULL");
        return GGL_ERR_FAILURE;
    }

    static uint8_t file_name_array[MAX_COMPONENT_FILE_NAME];
    GglBuffer file_name_buffer = (GglBuffer
    ) { .data = (uint8_t *) file_name_array, .len = MAX_COMPONENT_FILE_NAME };

    GglByteVec file_name_vector
        = { .buf = { .data = file_name_buffer.data, .len = 0 },
            .capacity = file_name_buffer.len };

    GglBuffer root_dir_buffer = (GglBuffer
    ) { .data = (uint8_t *) args->root_dir, .len = strlen(args->root_dir) };

    ret = ggl_byte_vec_append(&file_name_vector, root_dir_buffer);
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR("/"));
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &file_name_vector, (*component_name)->buf);
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR(".service\0"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    FILE *f = fopen((const char *) file_name_vector.buf.data, "wb");
    fwrite(response_buffer.data, sizeof(char), response_buffer.len, f);
    fclose(f);

    return GGL_ERR_OK;
}
