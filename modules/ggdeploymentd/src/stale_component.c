
#include "stale_component.h"
#include "component_store.h"
#include "deployment_model.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <gg/arena.h>
#include <gg/buffer.h>
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

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
GgError disable_and_unlink_service(
    GgBuffer *component_name, PhaseSelection phase
) {
    static uint8_t command_array[PATH_MAX];
    GgByteVec command_vec = GG_BYTE_VEC(command_array);

    GgError ret = gg_byte_vec_append(&command_vec, GG_STR("systemctl stop "));
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR("ggl."));
    gg_byte_vec_chain_append(&ret, &command_vec, *component_name);
    if (phase == INSTALL) {
        gg_byte_vec_chain_append(&ret, &command_vec, GG_STR(".install"));
    } else if (phase == BOOTSTRAP) {
        gg_byte_vec_chain_append(&ret, &command_vec, GG_STR(".bootstrap"));
    } else {
        // Incase of startup/run nothing to append
        assert(phase == RUN_STARTUP);
    }
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR(".service"));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create systemctl stop command.");
        return ret;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GG_LOGD("systemctl stop failed");
        }
        GG_LOGI(
            "systemctl stop exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GG_LOGE("systemctl stop did not exit normally");
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = gg_byte_vec_append(&command_vec, GG_STR("systemctl disable "));
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR("ggl."));
    gg_byte_vec_chain_append(&ret, &command_vec, *component_name);
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR(".service"));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create systemctl disable command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GG_LOGD("systemctl disable failed");
        }
        GG_LOGI(
            "systemctl disable exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GG_LOGE("systemctl disable did not exit normally");
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    // TODO: replace this with a better approach such as 'unlink'.
    ret = gg_byte_vec_append(&command_vec, GG_STR("rm /etc/systemd/system/"));
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR("ggl."));
    gg_byte_vec_chain_append(&ret, &command_vec, *component_name);
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR(".service"));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create rm /etc/systemd/system/[service] command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GG_LOGD("removing symlink failed");
        }
        GG_LOGI(
            "rm /etc/systemd/system/[service] exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GG_LOGE("rm /etc/systemd/system/[service] did not exit normally");
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    // TODO: replace this with a better approach such as 'unlink'.
    ret = gg_byte_vec_append(
        &command_vec, GG_STR("rm /usr/lib/systemd/system/")
    );
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR("ggl."));
    gg_byte_vec_chain_append(&ret, &command_vec, *component_name);
    gg_byte_vec_chain_append(&ret, &command_vec, GG_STR(".service"));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create rm /usr/lib/systemd/system/[service] command."
        );
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GG_LOGD("removing symlink failed");
        }
        GG_LOGI(
            "rm /usr/lib/systemd/system/[service] exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GG_LOGE("rm /usr/lib/systemd/system/[service] did not exit normally");
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = gg_byte_vec_append(&command_vec, GG_STR("systemctl daemon-reload"));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create systemctl daemon-reload command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GG_LOGE("systemctl daemon-reload failed");
        }
        GG_LOGI(
            "systemctl daemon-reload exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GG_LOGE("systemctl daemon-reload did not exit normally");
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = gg_byte_vec_append(&command_vec, GG_STR("systemctl reset-failed"));
    gg_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create systemctl reset-failed command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GG_LOGE("systemctl reset-failed failed");
        }
        GG_LOGI(
            "systemctl reset-failed exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GG_LOGE("systemctl reset-failed did not exit normally");
    }

    return GG_ERR_OK;
}

GgError cleanup_stale_versions(GgMap latest_components_map) {
    int recipe_dir_fd;
    GgError ret = get_recipe_dir_fd(&recipe_dir_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GG_LOGE("Failed to open recipe directory.");
        return GG_ERR_FAILURE;
    }

    struct dirent *entry = NULL;
    uint8_t component_name_array[NAME_MAX];
    GgBuffer component_name_buffer_iterator
        = { .data = component_name_array, .len = 0 };

    uint8_t version_array[NAME_MAX];
    GgBuffer version_buffer_iterator = { .data = version_array, .len = 0 };

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
            return ret;
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

            // Also delete the .script.install and .script.run and .service
            // files.
            (void) delete_recipe_script_and_service_files(
                &component_name_buffer_iterator
            );
        }
    }

    return GG_ERR_OK;
}
