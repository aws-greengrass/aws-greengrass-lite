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
#include <stdint.h>
#include <stdio.h>

#define MAX_RECIPE_BUF_SIZE 256000
#define MAX_UNIT_FILE_BUF_SIZE 2048

GglError convert_to_unit(Recipe2UnitArgs *args) {
    GglError ret = validate_args(args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GglBuffer recipe_str_buf;
    ret = open_file(args->recipe_path, &recipe_str_buf);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    static uint8_t big_buffer_for_bump[MAX_RECIPE_BUF_SIZE];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(big_buffer_for_bump));
    GglObject recipe_obj;

    ret = deserialize_file_content(
        args->recipe_path, recipe_str_buf, &balloc.alloc, &recipe_obj
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (recipe_obj.type != GGL_TYPE_MAP) {
        GGL_LOGE("recipe2unit", "Invalid recipe format provided");
        return GGL_ERR_FAILURE;
    }

    static uint8_t unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];
    GglBuffer response_buffer = (GglBuffer
    ) { .data = (uint8_t *) unit_file_buffer, .len = MAX_UNIT_FILE_BUF_SIZE };

    ret = generate_systemd_unit(recipe_obj, &response_buffer, args);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    FILE *f = fopen("test.service", "wb");
    fwrite(response_buffer.data, sizeof(char), response_buffer.len, f);
    fclose(f);

    return GGL_ERR_OK;
}
