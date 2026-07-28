// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "unit_file_generator.h"
#include "validate_args.h"
#include <assert.h>
#include <errno.h>
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
#include <stdio.h>

#define MAX_UNIT_FILE_BUF_SIZE 2048
#define MAX_COMPONENT_FILE_NAME 1024

// Builds the unit file path for a lifecycle phase. The returned path borrows a
// static buffer, so it is only valid until the next call.
static GgError unit_file_path(
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase,
    GgBuffer *path
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

    *path = file_name_vector.buf;
    return GG_ERR_OK;
}

// Remove the unit file of a phase the recipe no longer declares, so that a
// phase dropped by a recipe revision does not leave a stale unit behind.
static GgError remove_unit_file(
    Recipe2UnitArgs *args, GgObject **component_name, PhaseSelection phase
) {
    GgBuffer path = { 0 };
    GgError ret = unit_file_path(args, component_name, phase, &path);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if ((remove((const char *) path.data) != 0) && (errno != ENOENT)) {
        // Do nothing. The absence of file is okay.
        GG_LOGE("Failed to remove stale unit file: %d.", errno);
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

// A revised recipe may drop a phase that a previous revision declared. Remove
// those unit files, otherwise the stale phase is still run. Run/startup is
// deliberately excluded: its unit is the only one carrying
// WantedBy=greengrass-lite.target, so removing the file without also dropping
// that enablement would leave a dangling symlink behind.
static void remove_stale_unit_files(
    Recipe2UnitArgs *args,
    GgObject **component_name,
    const HasPhase *existing_phases
) {
    if (!existing_phases->has_bootstrap) {
        (void) remove_unit_file(args, component_name, BOOTSTRAP);
    }
    if (!existing_phases->has_install) {
        (void) remove_unit_file(args, component_name, INSTALL);
    }
}

static GgError create_unit_file(
    Recipe2UnitArgs *args,
    GgObject **component_name,
    PhaseSelection phase,
    GgBuffer *response_buffer
) {
    GgBuffer file_name = { 0 };
    GgError ret = unit_file_path(args, component_name, phase, &file_name);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    int fd = -1;
    ret = gg_file_open(file_name, O_WRONLY | O_CREAT | O_TRUNC, 0644, &fd);
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
    *existing_phases = (HasPhase) { 0 };

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
            "Recipes without at least 1 valid lifecycle step aren't currently supported by Greengrass nucleus lite"
        );

        GG_LOGW(
            "Note that in Greengrass nucleus lite, keys are case sensitive. Check the recipe reference for the correct casing."
        );
        return GG_ERR_INVALID;
    }

    remove_stale_unit_files(args, component_name, existing_phases);

    return GG_ERR_OK;
}

#ifdef GG_SDK_TESTING

#include <ftw.h>
#include <gg/test.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <unity.h>
#include <stdio.h>
#include <stdlib.h>

#define TEST_COMPONENT "com.example.StalePhase"

static int test_unlink_cb(
    const char *path, const struct stat *sb, int type, struct FTW *ftw
) {
    (void) sb;
    (void) type;
    (void) ftw;
    return remove(path);
}

static void remove_test_root(const char *root_dir) {
    (void) nftw(root_dir, test_unlink_cb, 8, FTW_DEPTH | FTW_PHYS);
}

static void make_test_root(char *root_dir, size_t len) {
    (void) snprintf(root_dir, len, "/tmp/ggl-recipe2unit-XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(root_dir));
}

static bool test_unit_exists(const char *root_dir, const char *suffix) {
    char path[PATH_MAX];
    (void) snprintf(
        path,
        sizeof(path),
        "%s/ggl." TEST_COMPONENT "%s.service",
        root_dir,
        suffix
    );
    return access(path, F_OK) == 0;
}

// Generates one unit file per phase, as a first deployment of a recipe
// declaring bootstrap, install and run would.
static void write_all_test_units(Recipe2UnitArgs *args, GgObject **name) {
    GgBuffer content = GG_STR("[Unit]\nDescription=test\n");
    GG_TEST_ASSERT_OK(create_unit_file(args, name, BOOTSTRAP, &content));
    GG_TEST_ASSERT_OK(create_unit_file(args, name, INSTALL, &content));
    GG_TEST_ASSERT_OK(create_unit_file(args, name, RUN_STARTUP, &content));
    TEST_ASSERT_TRUE(test_unit_exists(args->root_dir, ".bootstrap"));
    TEST_ASSERT_TRUE(test_unit_exists(args->root_dir, ".install"));
    TEST_ASSERT_TRUE(test_unit_exists(args->root_dir, ""));
}

// The phases a revised recipe declares drive which units survive. Mirrors the
// issue's scenario: install dropped, run replaced by startup.
GG_TEST_DEFINE(stale_units_removed_for_phases_absent_from_recipe) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    static Recipe2UnitArgs args;
    args = (Recipe2UnitArgs) { 0 };
    memcpy(args.root_dir, root_dir, strlen(root_dir) + 1);

    GgObject name_obj = gg_obj_buf(GG_STR(TEST_COMPONENT));
    GgObject *name = &name_obj;
    write_all_test_units(&args, &name);

    HasPhase revised = { .has_run_startup = true };
    remove_stale_unit_files(&args, &name, &revised);

    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".bootstrap"));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));

    remove_test_root(root_dir);
}

// Units of phases the revised recipe still declares must be left in place.
GG_TEST_DEFINE(units_kept_for_phases_present_in_recipe) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    static Recipe2UnitArgs args;
    args = (Recipe2UnitArgs) { 0 };
    memcpy(args.root_dir, root_dir, strlen(root_dir) + 1);

    GgObject name_obj = gg_obj_buf(GG_STR(TEST_COMPONENT));
    GgObject *name = &name_obj;
    write_all_test_units(&args, &name);

    HasPhase unchanged = { .has_bootstrap = true,
                           .has_install = true,
                           .has_run_startup = true };
    remove_stale_unit_files(&args, &name, &unchanged);

    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".bootstrap"));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));

    remove_test_root(root_dir);
}

// Dropping run/startup must NOT remove its unit: that unit is the only one
// carrying WantedBy=greengrass-lite.target, and the enablement symlink is not
// cleaned up here.
GG_TEST_DEFINE(run_startup_unit_kept_even_when_phase_absent) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    static Recipe2UnitArgs args;
    args = (Recipe2UnitArgs) { 0 };
    memcpy(args.root_dir, root_dir, strlen(root_dir) + 1);

    GgObject name_obj = gg_obj_buf(GG_STR(TEST_COMPONENT));
    GgObject *name = &name_obj;
    write_all_test_units(&args, &name);

    HasPhase install_only = { .has_install = true };
    remove_stale_unit_files(&args, &name, &install_only);

    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ""));
    TEST_ASSERT_TRUE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".bootstrap"));

    remove_test_root(root_dir);
}

// Removal tolerates units that were never generated.
GG_TEST_DEFINE(removing_absent_unit_files_succeeds) {
    char root_dir[PATH_MAX];
    make_test_root(root_dir, sizeof(root_dir));

    static Recipe2UnitArgs args;
    args = (Recipe2UnitArgs) { 0 };
    memcpy(args.root_dir, root_dir, strlen(root_dir) + 1);

    GgObject name_obj = gg_obj_buf(GG_STR(TEST_COMPONENT));
    GgObject *name = &name_obj;

    HasPhase none = { .has_run_startup = true };
    remove_stale_unit_files(&args, &name, &none);

    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".install"));
    TEST_ASSERT_FALSE(test_unit_exists(root_dir, ".bootstrap"));

    remove_test_root(root_dir);
}

#endif
