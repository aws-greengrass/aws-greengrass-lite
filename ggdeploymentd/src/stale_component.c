
#include "stale_component.h"
#include "component_store.h"
#include <sys/types.h>
#include <dirent.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static GglError delete_component_artifact(
    GglBuffer *component_name, GglBuffer *version_number, GglByteVec *root_path
) {
    const size_t INDEX_BEFORE_ADDITION = root_path->buf.len;
    GglError err
        = ggl_byte_vec_append(root_path, GGL_STR("/packages/artifacts/"));
    ggl_byte_vec_chain_append(&err, root_path, *component_name);
    ggl_byte_vec_chain_append(&err, root_path, GGL_STR("/"));
    ggl_byte_vec_chain_append(&err, root_path, *version_number);
    ggl_byte_vec_chain_append(&err, root_path, GGL_STR("\0"));

    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to create a delete-artifact path string.");
        return err;
    }

    char *exec_args[] = { "rm", "-rf", (char *) root_path->buf.data, NULL };
    pid_t exec_pid = -1;
    err = exec_command_with_child_wait(exec_args, &exec_pid);

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
    GglBuffer *component_name, GglBuffer *version_number, GglByteVec *root_path
) {
    const size_t INDEX_BEFORE_ADDITION = root_path->buf.len;
    GglError err
        = ggl_byte_vec_append(root_path, GGL_STR("/packages/recipes/"));
    ggl_byte_vec_chain_append(&err, root_path, *component_name);
    ggl_byte_vec_chain_append(&err, root_path, GGL_STR("-"));
    ggl_byte_vec_chain_append(&err, root_path, *version_number);
    // FIXME: Don't only support yaml extensions.
    ggl_byte_vec_chain_append(&err, root_path, GGL_STR(".yaml\0"));

    if (err != GGL_ERR_OK) {
        GGL_LOGE("Failed to create a delete-recipe path string.");
        return err;
    }

    char *exec_args[] = { "rm", "-f", (char *) root_path->buf.data, NULL };
    pid_t exec_pid = -1;
    err = exec_command_with_child_wait(exec_args, &exec_pid);

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
    GglBuffer *component_name, GglBuffer *version_number
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

    GglError err
        = delete_component_artifact(component_name, version_number, &root_path);

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

    char *extentions[] = { ".script.install.json", ".script.run", ".service" };

    for (size_t i = 0; i < (sizeof(extentions) / sizeof(char *)); i++) {
        GglBuffer buf = { .data = (uint8_t *) extentions[i],
                          .len = strlen(extentions[i]) };
        ggl_byte_vec_chain_append(&ret, &root_path, buf);

        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to create path for recipe script file deletion.");
            return ret;
        }

        char *exec_args[] = { "rm", "-f", (char *) root_path.buf.data, NULL };
        pid_t exec_pid = -1;
        ret = exec_command_with_child_wait(exec_args, &exec_pid);

        if (ret != GGL_ERR_OK) {
            GGL_LOGE("Failed to execute command to delete script files.");
            return ret;
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

static GglError disable_and_unlink_service(GglBuffer *component_name) {
    static uint8_t command_array[PATH_MAX];
    GglByteVec command_vec = GGL_BYTE_VEC(command_array);

    GglError ret
        = ggl_byte_vec_append(&command_vec, GGL_STR("sudo systemctl disable "));
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl disable command.");
        return ret;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    int system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl disable failed");
            return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "systemctl disable exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl disable did not exit normally");
        return GGL_ERR_FAILURE;
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = ggl_byte_vec_append(&command_vec, GGL_STR("sudo systemctl stop "));
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl stop command.");
        return ret;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl stop failed");
            return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "systemctl stop exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl stop did not exit normally");
        return GGL_ERR_FAILURE;
    }

    memset(command_array, 0, sizeof(command_array));
    command_vec.buf.len = 0;

    ret = ggl_byte_vec_append(&command_vec, GGL_STR("sudo systemctl unlink "));
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR("ggl."));
    ggl_byte_vec_chain_append(&ret, &command_vec, *component_name);
    ggl_byte_vec_chain_append(&ret, &command_vec, GGL_STR(".service"));
    ggl_byte_vec_chain_push(&ret, &command_vec, '\0');
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create systemctl unlink command.");
        return ret;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    system_ret = system((char *) command_vec.buf.data);
    if (WIFEXITED(system_ret)) {
        if (WEXITSTATUS(system_ret) != 0) {
            GGL_LOGE("systemctl unlink failed");
            return GGL_ERR_FAILURE;
        }
        GGL_LOGI(
            "systemctl unlink exited with child status %d\n",
            WEXITSTATUS(system_ret)
        );
    } else {
        GGL_LOGE("systemctl unlink did not exit normally");
        return GGL_ERR_FAILURE;
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

        if (entry == NULL) {
            return GGL_ERR_NOENTRY;
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
                &component_name_buffer_iterator, &version_buffer_iterator
            );

        } else {
            // Cannot find this component at all. Delete it!
            delete_component(
                &component_name_buffer_iterator, &version_buffer_iterator
            );

            // Also stop any running service for this component.
            disable_and_unlink_service(&component_name_buffer_iterator);

            // Also delete the .script.install and .script.run and .service
            // files.
            delete_recipe_script_and_service_files(
                &component_name_buffer_iterator
            );
        }
    } while (true);

    return GGL_ERR_OK;
}
