
#include "stale_component.h"
#include "component_store.h"
#include "deployment_model.h"
#include <dirent.h>
#include <errno.h>
#include <ftw.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
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
        GGL_LOGW("Failed to remove file %s.", fpath);
    }

    // Ignore the return code and keep deleting other files.
    return 0;
}

static int remove_all_files(char *path) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    return nftw(path, unlink_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static GglError delete_component_artifact(
    GglBuffer component_name,
    GglBuffer version_number,
    GglByteVec *root_path,
    bool delete_all_versions
) {
    const size_t INDEX_BEFORE_ADDITION = root_path->buf.len;

    // Delete artifacts.
    GglError err
        = ggl_byte_vec_append(root_path, GGL_STR("/packages/artifacts/"));
    ggl_byte_vec_chain_append(&err, root_path, component_name);

    if (delete_all_versions == false) {
        ggl_byte_vec_chain_append(&err, root_path, GGL_STR("/"));
        ggl_byte_vec_chain_append(&err, root_path, version_number);
        ggl_byte_vec_chain_append(&err, root_path, GGL_STR("\0"));
    } else {
        ggl_byte_vec_chain_append(&err, root_path, GGL_STR("\0"));
    }

    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to create a delete-artifact path string.");
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
    err = ggl_byte_vec_append(
        root_path, GGL_STR("/packages/artifacts-unarchived/")
    );
    ggl_byte_vec_chain_append(&err, root_path, component_name);

    if (delete_all_versions == false) {
        ggl_byte_vec_chain_append(&err, root_path, GGL_STR("/"));
        ggl_byte_vec_chain_append(&err, root_path, version_number);
        ggl_byte_vec_chain_append(&err, root_path, GGL_STR("\0"));
    } else {
        ggl_byte_vec_chain_append(&err, root_path, GGL_STR("\0"));
    }

    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to create a delete-artifact path string.");
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

static GglError delete_component_recipe(
    GglBuffer component_name, GglBuffer version_number, GglByteVec *root_path
) {
    const size_t INDEX_BEFORE_ADDITION = root_path->buf.len;
    GglError err
        = ggl_byte_vec_append(root_path, GGL_STR("/packages/recipes/"));
    ggl_byte_vec_chain_append(&err, root_path, component_name);
    ggl_byte_vec_chain_append(&err, root_path, GGL_STR("-"));
    ggl_byte_vec_chain_append(&err, root_path, version_number);

    // Store index so that we can restore the vector to this state.
    const size_t INDEX_BEFORE_FILE_EXTENTION = root_path->buf.len;
    const char *extentions[] = { ".json\0", ".yaml\0", ".yml\0" };

    for (size_t i = 0; i < (sizeof(extentions) / sizeof(char *)); i++) {
        GglBuffer buf = { .data = (uint8_t *) extentions[i],
                          .len = strlen(extentions[i]) };
        ggl_byte_vec_chain_append(&err, root_path, buf);

        if (err != GGL_ERR_OK) {
            GGL_LOGE("Failed to create a delete-recipe path string.");
            break;
        }

        int status = remove((char *) root_path->buf.data);

        if (status == EACCES) {
            GGL_LOGW(
                "Failed to delete the file %s. Permission denied.",
                root_path->buf.data
            );
        } else if (status == EPERM) {
            GGL_LOGW(
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

static GglError delete_component(
    GglBuffer component_name, GglBuffer version_number, bool delete_all_versions
) {
    GGL_LOGD(
        "Removing component %.*s with version %.*s as it is marked as stale",
        (int) component_name.len,
        component_name.data,
        (int) version_number.len,
        version_number.data
    );

    // Remove component from config as we use that as source of truth for active
    // running components
    GglError ret
        = ggl_gg_config_delete(GGL_BUF_LIST(GGL_STR("services"), component_name)
        );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to delete component information from the configuration."
        );
        return ret;
    }
    GGL_LOGD(
        "Removed configuration of stale component %.*s",
        (int) component_name.len,
        component_name.data
    );

    static uint8_t root_path_mem[PATH_MAX];
    memset(root_path_mem, 0, sizeof(root_path_mem));
    GglBuffer root_path_buffer = GGL_BUF(root_path_mem);

    ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &root_path_buffer
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get root path from config.");
        return ret;
    }

    // Remove the trailing slash.
    if ((root_path_buffer.len != 0)
        && (root_path_buffer.data[root_path_buffer.len - 1] == '/')) {
        root_path_buffer.len--;
    }

    GglByteVec root_path = { .buf = { .data = root_path_buffer.data,
                                      .len = root_path_buffer.len },
                             .capacity = sizeof(root_path_mem) };

    GglError err = delete_component_artifact(
        component_name, version_number, &root_path, delete_all_versions
    );

    if (err != GGL_ERR_OK) {
        return err;
    }

    err = delete_component_recipe(component_name, version_number, &root_path);

    return err;
}

static GglError delete_recipe_script_and_service_files(GglBuffer *component_name
) {
    static uint8_t root_path_mem[PATH_MAX];
    memset(root_path_mem, 0, sizeof(root_path_mem));
    GglBuffer root_path_buffer = GGL_BUF(root_path_mem);

    GglError ret = ggl_gg_config_read_str(
        GGL_BUF_LIST(GGL_STR("system"), GGL_STR("rootPath")), &root_path_buffer
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGW("Failed to get root path from config.");
        return ret;
    }

    GglByteVec root_path = { .buf = { .data = root_path_buffer.data,
                                      .len = root_path_buffer.len },
                             .capacity = sizeof(root_path_mem) };

    ret = ggl_byte_vec_append(&root_path, GGL_STR("/ggl."));
    ggl_byte_vec_chain_append(&ret, &root_path, *component_name);

    // Store index so that we can restore the vector to this state.
    const size_t INDEX_BEFORE_FILE_EXTENTION = root_path.buf.len;

    char *extentions[] = { ".bootstrap.json", ".install.service", ".service" };

    for (size_t i = 0; i < (sizeof(extentions) / sizeof(char *)); i++) {
        GglBuffer buf = { .data = (uint8_t *) extentions[i],
                          .len = strlen(extentions[i]) };
        ggl_byte_vec_chain_append(&ret, &root_path, buf);

        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create path for recipe script file deletion.");
            return ret;
        }

        int status = remove((char *) root_path.buf.data);

        if (status == EACCES) {
            GGL_LOGW(
                "Failed to delete the file %s. Permission denied.",
                root_path.buf.data
            );
        } else if (status == EPERM) {
            GGL_LOGW(
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
GglError disable_and_unlink_service(
    GglBuffer *component_name, PhaseSelection phase
) {
    static uint8_t command_array[PATH_MAX];
    GglByteVec command_vec = GGL_BYTE_VEC(command_array);

    GglError ret
        = ggl_byte_vec_append(&command_vec, GGL_STR("systemctl stop "));
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    if (phase == INSTALL) {
        ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".install"));
    }
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl stop command.");
        return ret;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl stop failed");
            // return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "systemctl stop exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl stop did not exit normally");
        // return GGL_ERR_FAILURE;
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = ggl_byte_vec_append(&command_vec, GGL_STR("systemctl disable "));
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl disable command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl disable failed");
            // return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "systemctl disable exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl disable did not exit normally");
        // return GGL_ERR_FAILURE;
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    // TODO: replace this with a better approach such as 'unlink'.
    ret = ggl_byte_vec_append(&command_vec, GGL_STR("rm /etc/systemd/system/"));
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create rm /etc/systemd/system/[service] command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("removing symlink failed");
            // return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "rm /etc/systemd/system/[service] exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("rm /etc/systemd/system/[service] did not exit normally");
        // return GGL_ERR_FAILURE;
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    // TODO: replace this with a better approach such as 'unlink'.
    ret = ggl_byte_vec_append(
        &command_vec, GGL_STR("rm /usr/lib/systemd/system/")
    );
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE(
            "Failed to create rm /usr/lib/systemd/system/[service] command."
        );
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("removing symlink failed");
            // return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "rm /usr/lib/systemd/system/[service] exited with child status "
            "%d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("rm /usr/lib/systemd/system/[service] did not exit normally");
        // return GGL_ERR_FAILURE;
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = ggl_byte_vec_append(&command_vec, GGL_STR("systemctl daemon-reload"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl daemon-reload command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl daemon-reload failed");
        }
        GGL_LOGI(
            "systemctl daemon-reload exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl daemon-reload did not exit normally");
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = ggl_byte_vec_append(&command_vec, GGL_STR("systemctl reset-failed"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl reset-failed command.");
        return ret;
    }

    // TODO: replace system call with platform independent function.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl reset-failed failed");
        }
        GGL_LOGI(
            "systemctl reset-failed exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl reset-failed did not exit normally");
    }

    return GGL_ERR_OK;
}

GglError cleanup_stale_versions(GglMap latest_components_map) {
    int recipe_dir_fd;
    GglError ret = get_recipe_dir_fd(&recipe_dir_fd);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // iterate through recipes in the directory
    DIR *dir = fdopendir(recipe_dir_fd);
    if (dir == NULL) {
        GGL_LOGE("Failed to open recipe directory.");
        return GGL_ERR_FAILURE;
    }

    struct dirent *entry = NULL;
    uint8_t component_name_array[NAME_MAX];
    GglBuffer component_name_buffer_iterator
        = { .data = component_name_array, .len = 0 };

    uint8_t version_array[NAME_MAX];
    GglBuffer version_buffer_iterator = { .data = version_array, .len = 0 };

    do {
        ret = iterate_over_components(
            dir,
            &component_name_buffer_iterator,
            &version_buffer_iterator,
            &entry
        );

        if ((entry == NULL) || (ret == GGL_ERR_NOENTRY)) {
            // No more entries to go over.
            break;
        }

        if (ret != GGL_ERR_OK) {
            return ret;
        }

        // Try to find this component in the map.
        GglObject *component_version = NULL;
        if (ggl_map_get(
                latest_components_map,
                component_name_buffer_iterator,
                &component_version
            )
            == true) {
            if (ggl_buffer_eq(version_buffer_iterator, component_version->buf)
                == true) {
                // The component name and version matches. Skip over it.
                continue;
            }

            // The component name matches but the version number doesn't
            // match. Delete it!
            delete_component(
                component_name_buffer_iterator, version_buffer_iterator, false
            );
        } else {
            // Cannot find this component at all. Delete it!
            delete_component(
                component_name_buffer_iterator, version_buffer_iterator, true
            );

            // Also stop any running service for this component.
            disable_and_unlink_service(
                &component_name_buffer_iterator, RUN_STARTUP
            );
            disable_and_unlink_service(
                &component_name_buffer_iterator, INSTALL
            );
            disable_and_unlink_service(
                &component_name_buffer_iterator, BOOTSTRAP
            );

            // Also delete the .script.install and .script.run and .service
            // files.
            delete_recipe_script_and_service_files(
                &component_name_buffer_iterator
            );
        }
    } while (true);

    return GGL_ERR_OK;
}
