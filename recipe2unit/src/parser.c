// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/recipe2unit.h"
#include "unit_file_generator.h"
#include "validate_args.h"
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/vector.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_UNIT_FILE_BUF_SIZE 2048
#define MAX_COMPONENT_FILE_NAME 1024

static GglError create_unit_file(
    Recipe2UnitArgs *args,
    GglObject **component_name,
    bool is_install,
    GglBuffer *response_buffer
) {
    static uint8_t file_name_array[MAX_COMPONENT_FILE_NAME];
    GglBuffer file_name_buffer = (GglBuffer
    ) { .data = (uint8_t *) file_name_array, .len = MAX_COMPONENT_FILE_NAME };

    GglByteVec file_name_vector
        = { .buf = { .data = file_name_buffer.data, .len = 0 },
            .capacity = file_name_buffer.len };

    GglBuffer root_dir_buffer = (GglBuffer
    ) { .data = (uint8_t *) args->root_dir, .len = strlen(args->root_dir) };

    GglError ret = ggl_byte_vec_append(&file_name_vector, root_dir_buffer);
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR("/"));
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &file_name_vector, (*component_name)->buf);
    if (is_install) {
        ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR(".install"));
    }
    ggl_byte_vec_chain_append(&ret, &file_name_vector, GGL_STR(".service\0"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    int fd = -1;
    ret = ggl_file_open(
        file_name_vector.buf, O_WRONLY | O_CREAT | O_TRUNC, 0644, &fd
    );
    GGL_CLEANUP(cleanup_close, fd);

    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to open/create a unit file");
        return GGL_ERR_FAILURE;
    }

    ret = ggl_file_write(fd, *response_buffer);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to write to the unit file.");
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError convert_to_unit(
    Recipe2UnitArgs *args,
    GglAlloc *alloc,
    GglObject *recipe_obj,
    GglObject **component_name
) {
    GglError ret;
    *component_name = NULL;
    bool is_install = false;

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
        GGL_LOGI("No recipe found");
        return ret;
    }

    // Note: currently, if we have both run and startup phases,
    // we will only select startup for the script and service file
    static uint8_t install_unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];

    static uint8_t run_startup_unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];
    GglBuffer install_response_buffer
        = (GglBuffer) { .data = (uint8_t *) install_unit_file_buffer,
                        .len = MAX_UNIT_FILE_BUF_SIZE };
    GglBuffer run_startup_response_buffer
        = (GglBuffer) { .data = (uint8_t *) run_startup_unit_file_buffer,
                        .len = MAX_UNIT_FILE_BUF_SIZE };

    ret = generate_systemd_unit(
        &recipe_obj->map,
        &install_response_buffer,
        args,
        component_name,
        INSTALL
    );
    if (*component_name == NULL) {
        GGL_LOGE("Component name was NULL");
        return GGL_ERR_FAILURE;
    }

    if (ret == GGL_ERR_NOENTRY) {
        GGL_LOGW("No Install phase present");

    } else if (ret == GGL_ERR_OK) {
        return ret;
    } else {
        is_install = true;
        ret = create_unit_file(
            args, component_name, is_install, &install_response_buffer
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create the install unit file.");
            return ret;
        }
    }

    ret = generate_systemd_unit(
        &recipe_obj->map,
        &run_startup_response_buffer,
        args,
        component_name,
        RUN_STARTUP
    );

    if (ret == GGL_ERR_NOENTRY) {
        GGL_LOGW("No run or phase present");
    } else if (ret != GGL_ERR_OK) {
        return ret;
    } else {
        is_install = false;
        ret = create_unit_file(
            args, component_name, is_install, &run_startup_response_buffer
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create the run or startup unit file.");
            return ret;
        }
        GGL_LOGD("Created run or startup unit file.");
    }

    return GGL_ERR_OK;
}
