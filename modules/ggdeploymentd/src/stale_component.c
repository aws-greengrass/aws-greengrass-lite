
#include "stale_component.h"
#include "component_store.h"
#include "deployment_model.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/docker_artifact_cleanup.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// Forward declare structure for use in the function below.
struct stat;

static int unlink_cb(
    const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf
) {
    (void) sb;
    (void) typeflag;
    (void) ftwbuf;

    int rv = remove(fpath);

    if (rv) {
        GG_LOGW("Failed to remove file %s.", fpath);
    }

    // Ignore the return code and keep deleting other files.
    return 0;
}

static int remove_all_files(char *path) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static GgError delete_component_artifact(
    GgBuffer component_name,
    GgBuffer version_number,
    GgByteVec *root_path,
    bool delete_all_versions
) {
    const size_t INDEX_BEFORE_ADDITION = root_path->buf.len;

    // Delete Docker artifacts
    int root_path_fd = -1;
    if (gg_dir_open(root_path->buf, 0, false, &root_path_fd) == GG_ERR_OK) {
        GG_LOGT("Attempting docker artifact removal");
        ggl_docker_artifact_cleanup(
            root_path_fd, component_name, version_number
        );
        (void) gg_close(root_path_fd);
    }

    // Delete artifacts.
    GgError err = gg_byte_vec_append(root_path, GG_STR("/packages/artifacts/"));
    gg_byte_vec_chain_append(&err, root_path, component_name);

    if (delete_all_versions == false) {
        gg_byte_vec_chain_append(&err, root_path, GG_STR("/"));
        gg_byte_vec_chain_append(&err, root_path, version_number);
        gg_byte_vec_chain_append(&err, root_path, GG_STR("\0"));
    } else {
        gg_byte_vec_chain_append(&err, root_path, GG_STR("\0"));
    }

    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to create a delete-artifact path string.");
        return err;
    }

    (void) remove_all_files((char *) root_path->buf.data);

    // We should reset the index regardless of the error code in case caller
    // does not exit.
    root_path->buf.len = INDEX_BEFORE_ADDITION;
    memset(
        &(root_path->buf.data[INDEX_BEFORE_ADDITION]),
        0,
        root_path->capacity - INDEX_BEFORE_ADDITION
    );

    // Delete unarchived artifacts.
    err = gg_byte_vec_append(
        root_path, GG_STR("/packages/artifacts-unarchived/")
    );
    gg_byte_vec_chain_append(&err, root_path, component_name);

    if (delete_all_versions == false) {
        gg_byte_vec_chain_append(&err, root_path, GG_STR("/"));
        gg_byte_vec_chain_append(&err, root_path, version_number);
        gg_byte_vec_chain_append(&err, root_path, GG_STR("\0"));
    } else {
        gg_byte_vec_chain_append(&err, root_path, GG_STR("\0"));
    }

    if (err != GG_ERR_OK) {
        GG_LOGE("Failed to create a delete-artifact path string.");
        return err;
    }

    (void) remove_all_files((char *) root_path->buf.data);

    // We should reset the index regardless of the error code in case caller
    // does not exit.
    root_path->buf.len = INDEX_BEFORE_ADDITION;
    memset(
        &(root_path->buf.data[INDEX_BEFORE_ADDITION]),
        0,
        root_path->capacity - INDEX_BEFORE_ADDITION
    );

    return err;
}

static GgError delete_component_recipe(
    GgBuffer component_name, GgBuffer version_number, GgByteVec *root_path
) {
    const size_t INDEX_BEFORE_ADDITION = root_path->buf.len;
    GgError err = gg_byte_vec_append(root_path, GG_STR("/packages/recipes/"));
    gg_byte_vec_chain_append(&err, root_path, component_name);
    gg_byte_vec_chain_append(&err, root_path, GG_STR("-"));
    gg_byte_vec_chain_append(&err, root_path, version_number);

    // Store index so that we can restore the vector to this state.
    const size_t INDEX_BEFORE_FILE_EXTENTION = root_path->buf.len;
    const char *extentions[] = { ".json\0", ".yaml\0", ".yml\0" };

    for (size_t i = 0; i < (sizeof(extentions) / sizeof(char *)); i++) {
        GgBuffer buf = { .data = (uint8_t *) extentions[i],
                         .len = strlen(extentions[i]) };
        gg_byte_vec_chain_append(&err, root_path, buf);

        if (err != GG_ERR_OK) {
            GG_LOGE("Failed to create a delete-recipe path string.");
            break;
        }

        int status = remove((char *) root_path->buf.data);

        if (status == EACCES) {
            GG_LOGW(
                "Failed to delete the file %s. Permission denied.",
                root_path->buf.data
            );
        } else if (status == EPERM) {
            GG_LOGW(
                "Failed to delete the file %s. It is a directory.",
                root_path->buf.data
            );
        } else {
            // Do nothing. The absence of file is okay.
        }

        // Restore vector state with only the component name added.
        root_path->buf.len = INDEX_BEFORE_FILE_EXTENTION;
        memset(
            &root_path->buf.data[INDEX_BEFORE_FILE_EXTENTION],
            0,
            root_path->capacity - INDEX_BEFORE_FILE_EXTENTION
        );
    }
    // We should reset the index regardless of the error code in case caller
    // does not exit.
    root_path->buf.len = INDEX_BEFORE_ADDITION;
    memset(
        &(root_path->buf.data[INDEX_BEFORE_ADDITION]),
        0,
        root_path->capacity - INDEX_BEFORE_ADDITION
    );

    return err;
}

static GgError delete_component(
    GgBuffer component_name, GgBuffer version_number, bool delete_all_versions
) {
    // TODO: Remove docker image artifacts before deleting recipe if this
    // component is the only one to require this artifact.

    GG_LOGD(
        "Removing component %.*s with version %.*s as it is marked as stale",
        (int) component_name.len,
        component_name.data,
        (int) version_number.len,
        version_number.data
    );
    GgError ret;

    // Remove component from config as we use that as source of truth for active
    // running components
    if (delete_all_versions) {
        ret = ggl_gg_config_delete(
            GG_BUF_LIST(GG_STR("services"), component_name)
        );
        if (ret != GG_ERR_OK) {
            GG_LOGE(
                "Failed to delete component information from the configuration."
            );
            return ret;
        }
        GG_LOGD(
            "Removed configuration of stale component %.*s",
            (int) component_name.len,
            component_name.data
        );
    }

    static uint8_t root_path_mem[PATH_MAX];
    memset(root_path_mem, 0, sizeof(root_path_mem));

    GgArena alloc = gg_arena_init(GG_BUF(root_path_mem));
    GgBuffer root_path_buffer;

    ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootPath")),
        &alloc,
        &root_path_buffer
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get root path from config.");
        return ret;
    }

    // Remove the trailing slash.
    if ((root_path_buffer.len != 0)
        && (root_path_buffer.data[root_path_buffer.len - 1] == '/')) {
        root_path_buffer.len--;
    }

    GgByteVec root_path = { .buf = { .data = root_path_buffer.data,
                                     .len = root_path_buffer.len },
                            .capacity = sizeof(root_path_mem) };

    GgError err = delete_component_artifact(
        component_name, version_number, &root_path, delete_all_versions
    );

    if (err != GG_ERR_OK) {
        return err;
    }

    err = delete_component_recipe(component_name, version_number, &root_path);

    return err;
}

static GgError delete_recipe_script_and_service_files(GgBuffer *component_name
) {
    static uint8_t root_path_mem[PATH_MAX];
    memset(root_path_mem, 0, sizeof(root_path_mem));

    GgArena alloc = gg_arena_init(GG_BUF(root_path_mem));
    GgBuffer root_path_buffer;

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(GG_STR("system"), GG_STR("rootPath")),
        &alloc,
        &root_path_buffer
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Failed to get root path from config.");
        return ret;
    }

    GgByteVec root_path = { .buf = { .data = root_path_buffer.data,
                                     .len = root_path_buffer.len },
                            .capacity = sizeof(root_path_mem) };

    ret = gg_byte_vec_append(&root_path, GG_STR("/ggl."));
    gg_byte_vec_chain_append(&ret, &root_path, *component_name);

    // Store index so that we can restore the vector to this state.
    const size_t INDEX_BEFORE_FILE_EXTENTION = root_path.buf.len;

    const char *extentions[]
        = { ".bootstrap.service", ".install.service", ".service" };

    for (size_t i = 0; i < (sizeof(extentions) / sizeof(char *)); i++) {
        GgBuffer buf = { .data = (uint8_t *) extentions[i],
                         .len = strlen(extentions[i]) };
        gg_byte_vec_chain_append(&ret, &root_path, buf);

        if (ret != GG_ERR_OK) {
            GG_LOGE("Failed to create path for recipe script file deletion.");
            return ret;
        }

        int status = remove((char *) root_path.buf.data);

        if (status == EACCES) {
            GG_LOGW(
                "Failed to delete the file %s. Permission denied.",
                root_path.buf.data
            );
        } else if (status == EPERM) {
            GG_LOGW(
                "Failed to delete the file %s. It is a directory.",
                root_path.buf.data
            );
        } else {
            // Do nothing. The absence of file is okay.
        }

        // Restore vector state with only the component name added.
        root_path.buf.len = INDEX_BEFORE_FILE_EXTENTION;
        memset(
            &root_path.buf.data[INDEX_BEFORE_FILE_EXTENTION],
            0,
            root_path.capacity - INDEX_BEFORE_FILE_EXTENTION
        );
    }

    return ret;
}

static GgBuffer phase_unit_suffix(PhaseSelection phase) {
    switch (phase) {
    case INSTALL:
        return GG_STR(".install.service");
    case BOOTSTRAP:
        return GG_STR(".bootstrap.service");
    case RUN_STARTUP:
        break;
    }
    return GG_STR(".service");
}

// Replaced by unit tests so they can record commands instead of running them.
static int (*system_fn)(const char *command) = system;

// Runs @p command through system(). Returns GG_ERR_FAILURE unless it exits
// normally with status 0.
static GgError run_command(const char *command) {
    GG_LOGD("Command to execute: %s", command);
    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int system_ret = system_fn(command);
    if (!WIFEXITED(system_ret)) {
        GG_LOGE("%s did not exit normally", command);
        return GG_ERR_FAILURE;
    }
    GG_LOGI("%s exited with child status %d", command, WEXITSTATUS(system_ret));
    return (WEXITSTATUS(system_ret) == 0) ? GG_ERR_OK : GG_ERR_FAILURE;
}

// Runs `<prefix>ggl.<component_name><unit suffix for phase>`.
static GgError run_unit_command(
    GgBuffer prefix, GgBuffer component_name, PhaseSelection phase
) {
    static uint8_t command_buf[PATH_MAX];
    GgByteVec command_vec = GG_BYTE_VEC(command_buf);
    GgError ret = gg_byte_vec_append(&command_vec, prefix);
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR("ggl."));
    gg_byte_vec_chain_append(&ret, &command_vec, component_name);
    gg_byte_vec_chain_append(&ret, &command_vec, phase_unit_suffix(phase));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to create %.*s command for %.*s.",
            (int) prefix.len,
            prefix.data,
            (int) component_name.len,
            component_name.data
        );
        return ret;
    }
    return run_command((char *) command_vec.buf.data);
}

// Disables the unit for @p phase and removes its symlinks. Failures are
// logged by run_command() and otherwise ignored.
static void disable_and_remove_unit(
    GgBuffer component_name, PhaseSelection phase
) {
    // --no-reload: callers reload once per batch of unit changes. Each reload
    // blocks systemd's main loop, which also services the hardware watchdog.
    (void) run_unit_command(
        GG_STR("systemctl disable --no-reload "), component_name, phase
    );
    // TODO: replace this with a better approach such as 'unlink'.
    (void) run_unit_command(
        GG_STR("rm /etc/systemd/system/"), component_name, phase
    );
    (void) run_unit_command(
        GG_STR("rm /usr/lib/systemd/system/"), component_name, phase
    );
}

GgError disable_and_unlink_service(
    GgBuffer *component_name, PhaseSelection phase
) {
    (void) run_unit_command(GG_STR("systemctl stop "), *component_name, phase);
    disable_and_remove_unit(*component_name, phase);
    if (phase != RUN_STARTUP) {
        // The run unit has already been regenerated for the new version. Take
        // it out of greengrass-lite.target (without stopping the old one) so a
        // reboot before the run phase can't start it ahead of install.
        disable_and_remove_unit(*component_name, RUN_STARTUP);
    }
    if (run_command("systemctl reset-failed") != GG_ERR_OK) {
        GG_LOGE("systemctl reset-failed failed");
    }
    return GG_ERR_OK;
}

GgError systemd_daemon_reload(void) {
    GgError ret = run_command("systemctl daemon-reload");
    if (ret != GG_ERR_OK) {
        GG_LOGE("systemctl daemon-reload failed");
    }
    return ret;
}

// Records @p component_name in @p removed_components_out (with bytes copied
// into @p removed_names_storage), deduplicating against names already
// recorded. A no-op if either output pointer is NULL. On overflow of either
// storage, logs a warning and rolls back the partial byte copy without
// aborting the surrounding cleanup pass.
static void record_removed_component(
    GgBuffer component_name,
    GgByteVec *removed_names_storage,
    GgBufVec *removed_components_out
) {
    if ((removed_names_storage == NULL) || (removed_components_out == NULL)) {
        return;
    }

    for (size_t i = 0; i < removed_components_out->buf_list.len; i++) {
        if (gg_buffer_eq(
                component_name, removed_components_out->buf_list.bufs[i]
            )) {
            return;
        }
    }

    size_t name_offset = removed_names_storage->buf.len;
    GgError ret = gg_byte_vec_append(removed_names_storage, component_name);
    if (ret == GG_ERR_OK) {
        GgBuffer slice
            = { .data = removed_names_storage->buf.data + name_offset,
                .len = component_name.len };
        ret = gg_buf_vec_push(removed_components_out, slice);
    }
    if (ret != GG_ERR_OK) {
        // Roll back the partial copy so the storage offset stays consistent
        // with the bufvec.
        removed_names_storage->buf.len = name_offset;
        GG_LOGW(
            "Could not record removed component %.*s for fleet status update (ret=%d).",
            (int) component_name.len,
            component_name.data,
            (int) ret
        );
    }
}

GgError cleanup_stale_versions(
    GgMap latest_components_map,
    GgByteVec *removed_names_storage,
    GgBufVec *removed_components_out
) {
    // Both must be NULL or both must be non-NULL: the bufvec stores slices
    // into the byte vec, so they share a lifetime.
    assert((removed_names_storage == NULL) == (removed_components_out == NULL));

    int recipe_dir_fd;
    GgError ret = get_recipe_dir_fd(&recipe_dir_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GG_LOGE("Failed to open recipe directory.");
        (void) gg_close(recipe_dir_fd);
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_closedir, dir);

    struct dirent *entry = NULL;
    uint8_t component_name_array[NAME_MAX];
    GgBuffer component_name_buffer_iterator
        = { .data = component_name_array, .len = 0 };

    uint8_t version_array[NAME_MAX];
    GgBuffer version_buffer_iterator = { .data = version_array, .len = 0 };

    GgError result = GG_ERR_OK;
    bool units_removed = false;

    while (true) {
        ret = iterate_over_components(
            dir,
            &component_name_buffer_iterator,
            &version_buffer_iterator,
            &entry
        );

        if ((entry == NULL) || (ret == GG_ERR_NOENTRY)) {
            // No more entries to go over.
            break;
        }

        if (ret != GG_ERR_OK) {
            result = ret;
            break;
        }

        // NucleusLite has no deployed systemd unit — skip stale cleanup.
        if (gg_buffer_has_prefix(
                component_name_buffer_iterator,
                GG_STR("aws.greengrass.NucleusLite")
            )) {
            continue;
        }

        // Try to find this component in the map.
        GgObject *component_version = NULL;
        if (gg_map_get(
                latest_components_map,
                component_name_buffer_iterator,
                &component_version
            )) {
            if (gg_buffer_eq(
                    version_buffer_iterator, gg_obj_into_buf(*component_version)
                )) {
                // The component name and version matches. Skip over it.
                continue;
            }

            // The component name matches but the version number doesn't
            // match. Delete it!
            (void) delete_component(
                component_name_buffer_iterator, version_buffer_iterator, false
            );
        } else {
            // Cannot find this component at all. Delete it!
            (void) delete_component(
                component_name_buffer_iterator, version_buffer_iterator, true
            );

            // Record the removal so the caller can report UNINSTALLED to the
            // cloud. Multiple installed versions of the same removed
            // component reach this branch once each, so the helper
            // deduplicates against names already recorded.
            record_removed_component(
                component_name_buffer_iterator,
                removed_names_storage,
                removed_components_out
            );

            // Also stop any running service for this component.
            (void) disable_and_unlink_service(
                &component_name_buffer_iterator, RUN_STARTUP
            );
            (void) disable_and_unlink_service(
                &component_name_buffer_iterator, INSTALL
            );
            (void) disable_and_unlink_service(
                &component_name_buffer_iterator, BOOTSTRAP
            );
            units_removed = true;

            // Also delete the .script.install and .script.run and .service
            // files.
            (void) delete_recipe_script_and_service_files(
                &component_name_buffer_iterator
            );
        }
    }

    // One reload for all the units removed above.
    if (units_removed) {
        (void) systemd_daemon_reload();
    }

    return result;
}

#ifdef GG_SDK_TESTING

#include <gg/test.h>
#include <signal.h>
#include <unity.h>

#define FAKE_MAX_COMMANDS 16
#define FAKE_COMMAND_LEN 128

static char fake_commands[FAKE_MAX_COMMANDS][FAKE_COMMAND_LEN];
static size_t fake_command_count;
// Raw wait status returned by fake_system() for every command.
static int fake_status;

static int fake_system(const char *command) {
    TEST_ASSERT_LESS_THAN_UINT32(FAKE_MAX_COMMANDS, fake_command_count);
    TEST_ASSERT_LESS_THAN_UINT32(FAKE_COMMAND_LEN, strlen(command));
    (void) snprintf(
        fake_commands[fake_command_count], FAKE_COMMAND_LEN, "%s", command
    );
    fake_command_count += 1;
    return fake_status;
}

static void start_fake_system(int status) {
    fake_command_count = 0;
    fake_status = status;
    system_fn = fake_system;
}

static void stop_fake_system(void) {
    system_fn = system;
}

static void assert_commands(const char *const *expected, size_t count) {
    TEST_ASSERT_EQUAL_UINT32(count, fake_command_count);
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_EQUAL_STRING(expected[i], fake_commands[i]);
    }
}

// Raw wait status for a process that exited with status 1.
#define FAKE_EXIT_FAILURE (1 << 8)

GG_TEST_DEFINE(stale_component_disable_install_unit_without_reload) {
    start_fake_system(0);
    GgBuffer name = GG_STR("comp");

    TEST_ASSERT_EQUAL(GG_ERR_OK, disable_and_unlink_service(&name, INSTALL));

    // Stops and unlinks the install unit, then unlinks the run unit without
    // stopping it.
    const char *const EXPECTED[] = {
        "systemctl stop ggl.comp.install.service",
        "systemctl disable --no-reload ggl.comp.install.service",
        "rm /etc/systemd/system/ggl.comp.install.service",
        "rm /usr/lib/systemd/system/ggl.comp.install.service",
        "systemctl disable --no-reload ggl.comp.service",
        "rm /etc/systemd/system/ggl.comp.service",
        "rm /usr/lib/systemd/system/ggl.comp.service",
        "systemctl reset-failed",
    };
    assert_commands(EXPECTED, sizeof(EXPECTED) / sizeof(EXPECTED[0]));
    stop_fake_system();
}

GG_TEST_DEFINE(stale_component_disable_bootstrap_unit_without_reload) {
    start_fake_system(0);
    GgBuffer name = GG_STR("comp");

    TEST_ASSERT_EQUAL(GG_ERR_OK, disable_and_unlink_service(&name, BOOTSTRAP));

    // Stops and unlinks the bootstrap unit, then unlinks the run unit without
    // stopping it.
    const char *const EXPECTED[] = {
        "systemctl stop ggl.comp.bootstrap.service",
        "systemctl disable --no-reload ggl.comp.bootstrap.service",
        "rm /etc/systemd/system/ggl.comp.bootstrap.service",
        "rm /usr/lib/systemd/system/ggl.comp.bootstrap.service",
        "systemctl disable --no-reload ggl.comp.service",
        "rm /etc/systemd/system/ggl.comp.service",
        "rm /usr/lib/systemd/system/ggl.comp.service",
        "systemctl reset-failed",
    };
    assert_commands(EXPECTED, sizeof(EXPECTED) / sizeof(EXPECTED[0]));
    stop_fake_system();
}

GG_TEST_DEFINE(stale_component_disable_run_unit_without_reload) {
    start_fake_system(0);
    GgBuffer name = GG_STR("comp");

    TEST_ASSERT_EQUAL(
        GG_ERR_OK, disable_and_unlink_service(&name, RUN_STARTUP)
    );

    const char *const EXPECTED[] = {
        "systemctl stop ggl.comp.service",
        "systemctl disable --no-reload ggl.comp.service",
        "rm /etc/systemd/system/ggl.comp.service",
        "rm /usr/lib/systemd/system/ggl.comp.service",
        "systemctl reset-failed",
    };
    assert_commands(EXPECTED, sizeof(EXPECTED) / sizeof(EXPECTED[0]));
    stop_fake_system();
}

GG_TEST_DEFINE(stale_component_disable_continues_after_command_failures) {
    start_fake_system(FAKE_EXIT_FAILURE);
    GgBuffer name = GG_STR("comp");

    TEST_ASSERT_EQUAL(GG_ERR_OK, disable_and_unlink_service(&name, INSTALL));

    // Every command still runs, and none of them is a reload.
    TEST_ASSERT_EQUAL_UINT32(8, fake_command_count);
    for (size_t i = 0; i < fake_command_count; i++) {
        TEST_ASSERT_NULL(strstr(fake_commands[i], "daemon-reload"));
    }
    stop_fake_system();
}

GG_TEST_DEFINE(stale_component_daemon_reload_runs_single_reload) {
    start_fake_system(0);

    TEST_ASSERT_EQUAL(GG_ERR_OK, systemd_daemon_reload());

    const char *const EXPECTED[] = { "systemctl daemon-reload" };
    assert_commands(EXPECTED, sizeof(EXPECTED) / sizeof(EXPECTED[0]));
    stop_fake_system();
}

GG_TEST_DEFINE(stale_component_daemon_reload_reports_nonzero_exit) {
    start_fake_system(FAKE_EXIT_FAILURE);

    TEST_ASSERT_EQUAL(GG_ERR_FAILURE, systemd_daemon_reload());
    stop_fake_system();
}

GG_TEST_DEFINE(stale_component_daemon_reload_reports_abnormal_exit) {
    // Process killed by a signal rather than exiting.
    start_fake_system(SIGKILL);

    TEST_ASSERT_EQUAL(GG_ERR_FAILURE, systemd_daemon_reload());
    stop_fake_system();
}

#endif
