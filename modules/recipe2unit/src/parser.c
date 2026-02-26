// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "validate_args.h"
#include <assert.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/recipe.h>
#include <ggl/recipe2unit.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#define MAX_UNIT_FILE_BUF_SIZE 2048
#define MAX_COMPONENT_FILE_NAME 1024

static GgError create_unit_file(
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase,
    GgBuffer *response_buffer
) {
    static uint8_t file_name_array[MAX_COMPONENT_FILE_NAME];
    GgBuffer file_name_buffer = (GgBuffer
    ) { .data = (uint8_t *) file_name_array, .len = MAX_COMPONENT_FILE_NAME };

    GgByteVec file_name_vector
        = { .buf = { .data = file_name_buffer.data, .len = 0 },
            .capacity = file_name_buffer.len };

    GgBuffer root_dir_buffer = (GgBuffer) { .data = (uint8_t *) args->root_dir,
                                            .len = strlen(args->root_dir) };

    GgError ret = gg_byte_vec_append(&file_name_vector, root_dir_buffer);
    gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR("/"));
    gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR("ggl."));
    gg_byte_vec_chain_append(
        &ret, &file_name_vector, gg_obj_into_buf(**component_name)
    );
    if (phase == INSTALL) {
        gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR(".install"));
    } else if (phase == BOOTSTRAP) {
        gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR(".bootstrap"));
    } else {
        // Incase of startup/run nothing to append
        assert(phase == RUN_STARTUP);
    }
    gg_byte_vec_chain_append(&ret, &file_name_vector, GG_STR(".service\0"));
    if (ret != GG_ERR_OK) {
        return ret;
    }

    int fd = -1;
    ret = gg_file_open(
        file_name_vector.buf, O_WRONLY | O_CREAT | O_TRUNC, 0644, &fd
    );
    GG_CLEANUP(cleanup_close, fd);

    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to open/create a unit file");
        return GG_ERR_FAILURE;
    }

    ret = gg_file_write(fd, *response_buffer);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to write to the unit file.");
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError convert_to_unit(
    Recipe2UnitArgs *args,
    GgArena *alloc,
    GgObject *recipe_obj,
    GgObject **component_name,
    HasPhase *existing_phases
) {
    GgError ret;
    *component_name = NULL;

    ret = validate_args(args);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = ggl_recipe_get_from_file(
        args->root_path_fd,
        args->component_name,
        args->component_version,
        alloc,
        recipe_obj
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("No recipe found");
        return ret;
    }

    // Note: currently, if we have both run and startup phases,
    // we will only select startup for the script and service file
    static uint8_t unit_file_buffer[MAX_UNIT_FILE_BUF_SIZE];

    GgBuffer bootstrap_response_buffer = GG_BUF(unit_file_buffer);
    bootstrap_response_buffer.len = MAX_UNIT_FILE_BUF_SIZE;

    GG_LOGD("Attempting to find bootstrap phase from recipe");
    ret = generate_systemd_unit(
        gg_obj_into_map(*recipe_obj),
        &bootstrap_response_buffer,
        args,
        component_name,
        BOOTSTRAP
    );
    if (*component_name == NULL) {
        GG_LOGE("Component name was NULL");
        return GG_ERR_FAILURE;
    }

    if (ret == GG_ERR_NOENTRY) {
        GG_LOGD("No bootstrap phase present");

    } else if (ret != GG_ERR_OK) {
        return ret;
    } else {
        ret = create_unit_file(
            args, component_name, BOOTSTRAP, &bootstrap_response_buffer
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create the bootstrap unit file.");
            return ret;
        }
        existing_phases->has_bootstrap = true;
    }

    GgBuffer install_response_buffer = GG_BUF(unit_file_buffer);
    install_response_buffer.len = MAX_UNIT_FILE_BUF_SIZE;

    GgMap recipe = gg_obj_into_map(*recipe_obj);

    GG_LOGD("Attempting to find install phase from recipe");
    ret = generate_systemd_unit(
        recipe, &install_response_buffer, args, component_name, INSTALL
    );
    if (*component_name == NULL) {
        GG_LOGE("Component name was NULL");
        return GG_ERR_FAILURE;
    }

    if (ret == GG_ERR_NOENTRY) {
        GG_LOGD("No Install phase present");

    } else if (ret != GG_ERR_OK) {
        return ret;
    } else {
        ret = create_unit_file(
            args, component_name, INSTALL, &install_response_buffer
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create the install unit file.");
            return ret;
        }
        existing_phases->has_install = true;
    }

    GgBuffer run_startup_response_buffer = GG_BUF(unit_file_buffer);
    run_startup_response_buffer.len = MAX_UNIT_FILE_BUF_SIZE;

    GG_LOGD("Attempting to find run phase from recipe");
    ret = generate_systemd_unit(
        recipe, &run_startup_response_buffer, args, component_name, RUN_STARTUP
    );
    if (ret == GG_ERR_NOENTRY) {
        GG_LOGD("Neither run nor startup phase present");
    } else if (ret != GG_ERR_OK) {
        return ret;
    } else {
        ret = create_unit_file(
            args, component_name, RUN_STARTUP, &run_startup_response_buffer
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create the run or startup unit file.");
            return ret;
        }
        GG_LOGD("Created run or startup unit file.");
        existing_phases->has_run_startup = true;
    }

    if (existing_phases->has_bootstrap == false
        && existing_phases->has_install == false
        && existing_phases->has_run_startup == false) {
        GG_LOGE(
            "Recipes without at least 1 valid lifecycle step aren't currently supported by GGLite"
        );

        GG_LOGW(
            "Note that in GG Lite, keys are case sensitive. Check the recipe reference for the correct casing."
        );
        return GG_ERR_INVALID;
    }

    return GG_ERR_OK;
}
