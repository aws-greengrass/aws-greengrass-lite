// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "recipe_model.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <ggl/alloc.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/yaml_decode.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#define ARTIFACTS "artifacts"
#define RECIPES "recipes"
#define JSON_EXTENSION ".json"
#define EXTENSION_LEN (sizeof(JSON_EXTENSION) - 1)
#define YAML_EXTENSION ".yaml"

static void ggl_deployment_listen(void);
static void handle_deployment(GgdeploymentdDeployment);
static GglError load_recipe(GglBuffer recipe_dir, Recipe *recipe);
static GglError copy_artifacts(GglBuffer artifact_dir, Recipe *recipe);
static GglError read_recipe(char *recipe_path, Recipe *recipe);
static GglError parse_recipe(GglMap recipe_map, Recipe *recipe);
static GglError create_component_directory(
    Recipe *recipe, char **directory_path, char *type
);
static void create_directories(const char *path, size_t path_size);
static GglError copy_file(const char *src_path, const char *dest_path);
bool shutdown = false;
bool recipe_is_json = false;

void *ggl_deployment_handler_start(void *ctx) {
    (void) ctx;
    ggl_deployment_listen();
    return NULL;
}

void ggl_deployment_handler_stop(void) {
    shutdown = true;
}

static void ggl_deployment_listen(void) {
    while (!shutdown) {
        GgdeploymentdDeployment deployment = ggl_deployment_queue_poll();

        GGL_LOGI(
            "deployment-handler",
            "Received deployment in the queue. Processing deployment."
        );

        handle_deployment(deployment);
    }
}

static void handle_deployment(GgdeploymentdDeployment deployment) {
    if (deployment.deployment_stage == GGDEPLOYMENT_DEFAULT) {
        if (deployment.deployment_type == GGDEPLOYMENT_LOCAL) {
            GgdeploymentdDeploymentDocument deployment_doc
                = deployment.deployment_document;
            Recipe recipe;
            if (deployment_doc.recipe_directory_path.len != 0) {
                load_recipe(deployment_doc.recipe_directory_path, &recipe);
            }
            if (deployment_doc.artifact_directory_path.len != 0) {
                copy_artifacts(deployment_doc.artifact_directory_path, &recipe);
            }
        }

        if (deployment.deployment_type == GGDEPLOYMENT_IOT_JOBS) {
            // not yet supported
            GGL_LOGE(
                "deployment-handler",
                "IoT Jobs deployments are not currently supported."
            );
        }

        if (deployment.deployment_type == GGDEPLOYMENT_SHADOW) {
            // not yet supported
            GGL_LOGE(
                "deployment-handler",
                "Shadow deployments are not currently supported."
            );
        }
    }
}

static GglError load_recipe(GglBuffer recipe_dir, Recipe *recipe) {
    assert(recipe_dir.data[recipe_dir.len - 1] == '\0');

    // open and iterate through the provided recipe directory
    struct dirent *entry;
    DIR *dir = opendir((char *) recipe_dir.data);

    GGL_LOGI(
        "test-depl-handler",
        "%.*s",
        (int) recipe_dir.len,
        (char *) recipe_dir.data
    );

    if (dir == NULL) {
        GGL_LOGE(
            "deployment-handler",
            "Deployment document contains invalid recipe directory path."
        );
        return GGL_ERR_INVALID;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while ((entry = readdir(dir)) != NULL) {
        // check that the entry is not another directory
        if (entry->d_type != DT_DIR) {
            // determine if recipe is in json or yaml format
            size_t filename_len = strlen(entry->d_name);
            if (filename_len > EXTENSION_LEN
                && strcmp(
                       entry->d_name + filename_len - EXTENSION_LEN,
                       JSON_EXTENSION
                   ) == 0) {
                recipe_is_json = true;
            }
            // TODO: support .yml and .yaml, currently only .yaml supported

            // build full path to recipe file
            size_t path_size
                = strlen((char *) recipe_dir.data) + filename_len + 2;
            GGL_LOGI("test", "path size: %zu", path_size);
            char *full_path = malloc(path_size);
            snprintf(
                full_path,
                path_size,
                "%s/%s",
                (char *) recipe_dir.data,
                entry->d_name
            );

            // parse recipe into Recipe model
            GglError read_err = read_recipe(full_path, recipe);

            if (read_err != GGL_ERR_OK) {
                free(full_path);
                return read_err;
            }

            // create recipe directory if it does not exist
            char *directory_path = NULL;
            GglError create_directory_err
                = create_component_directory(recipe, &directory_path, RECIPES);
            if (create_directory_err != GGL_ERR_OK) {
                free(directory_path);
                free(full_path);
                return create_directory_err;
            }

            // build recipe file destination path
            // this is the path to the directory plus the recipe file name
            // the recipe file name follows the format
            // componentName-componentVersion.json
            size_t recipe_file_name_size = recipe->component_name.len
                + recipe->component_version.len + EXTENSION_LEN + 10;
            char *recipe_file_dest_path
                = malloc(strlen(directory_path) + recipe_file_name_size);
            recipe_file_dest_path[0] = '\0';

            strncat(
                recipe_file_dest_path, directory_path, strlen(directory_path)
            );
            strncat(recipe_file_dest_path, "/", 1);
            strncat(
                recipe_file_dest_path,
                (char *) recipe->component_name.data,
                recipe->component_name.len
            );
            strncat(recipe_file_dest_path, "-", 1);
            strncat(
                recipe_file_dest_path,
                (char *) recipe->component_version.data,
                recipe->component_version.len
            );
            if (recipe_is_json) {
                strncat(recipe_file_dest_path, JSON_EXTENSION, EXTENSION_LEN);
            } else {
                strncat(recipe_file_dest_path, YAML_EXTENSION, EXTENSION_LEN);
            }

            // copy the recipe from the path provided in the deployment doc to
            // the device
            GglError copy_file_err
                = copy_file(full_path, recipe_file_dest_path);
            if (copy_file_err != GGL_ERR_OK) {
                free(directory_path);
                free(recipe_file_dest_path);
                free(full_path);
                return copy_file_err;
            }

            free(directory_path);
            free(recipe_file_dest_path);
            free(full_path);
        }
    }

    return GGL_ERR_OK;
}

static GglError read_recipe(char *recipe_path, Recipe *recipe) {
    // open and read the contents of the recipe file path provided into a buffer
    FILE *file = fopen(recipe_path, "r");
    if (file == NULL) {
        GGL_LOGE("deployment-handler", "Recipe file path invalid.");
        return GGL_ERR_INVALID;
    }

    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *buff = malloc((size_t) file_size + 1);
    if (buff == NULL) {
        GGL_LOGE(
            "deployment-handler",
            "Failed to allocate memory to read recipe file."
        );
        fclose(file);
        return GGL_ERR_FAILURE;
    }

    size_t bytes_read = fread(buff, 1, (size_t) file_size, file);
    if (bytes_read < (size_t) file_size) {
        GGL_LOGE("deployment-handler", "Failed to read recipe file.");
        fclose(file);
        free(buff);
        return GGL_ERR_FAILURE;
    }

    buff[file_size] = '\0';
    fclose(file);

    // buff now contains file contents, parse into Recipe struct
    GglBuffer recipe_content
        = { .data = (uint8_t *) buff, .len = (size_t) file_size };
    GglObject val;

    static uint8_t decode_mem[GGL_RECIPE_CONTENT_MAX_SIZE * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(decode_mem));

    // parse depending on file type
    GglError decode_err;
    if (recipe_is_json) {
        decode_err
            = ggl_json_decode_destructive(recipe_content, &balloc.alloc, &val);
    } else {
        decode_err = ggl_yaml_decode(recipe_content, &balloc.alloc, &val);
    }
    free(buff);

    if (decode_err != GGL_ERR_OK) {
        return decode_err;
    }

    // val should now contain the json object we need, create Recipe object out
    // of it
    GglMap decoded_val = val.map;
    GglError parse_err = parse_recipe(decoded_val, recipe);

    return parse_err;
}

static GglError parse_recipe(GglMap recipe_map, Recipe *recipe) {
    GglObject *component_name;
    if (ggl_map_get(recipe_map, GGL_STR("ComponentName"), &component_name)) {
        if (component_name == NULL) {
            GGL_LOGE(
                "deployment-handler",
                "Malformed recipe, component name not found."
            );
            return GGL_ERR_INVALID;
        }
        recipe->component_name = component_name->buf;
    } else {
        GGL_LOGE(
            "deployment-handler", "Malformed recipe, component name not found."
        );
        return GGL_ERR_INVALID;
    }

    GglObject *component_version;
    if (ggl_map_get(
            recipe_map, GGL_STR("ComponentVersion"), &component_version
        )) {
        if (component_version == NULL) {
            GGL_LOGE(
                "deployment-handler",
                "Malformed recipe, component version not found."
            );
            return GGL_ERR_INVALID;
        }
        recipe->component_version = component_version->buf;
    } else {
        GGL_LOGE(
            "deployment-handler",
            "Malformed recipe, component version not found."
        );
        return GGL_ERR_INVALID;
    }

    return GGL_ERR_OK;
}

static GglError create_component_directory(
    Recipe *recipe, char **directory_path, char *type
) {
    // build path for the requested directory
    // type parameter determines if we create directories for component recipes
    // or artifacts
    const char *root_path = "/";
    size_t full_path_size = strlen(root_path) + strlen(type)
        + recipe->component_name.len + recipe->component_version.len + 4;
    *directory_path = malloc(full_path_size);
    *directory_path[0] = '\0';

    strncat(*directory_path, root_path, strlen(root_path));
    strncat(*directory_path, "/", 1);
    strncat(*directory_path, type, strlen(type));
    strncat(*directory_path, "/", 1);
    strncat(
        *directory_path,
        (char *) recipe->component_name.data,
        recipe->component_name.len
    );
    strncat(*directory_path, "/", 1);
    strncat(
        *directory_path,
        (char *) recipe->component_version.data,
        recipe->component_version.len
    );

    // check if the directory exists
    struct stat st;
    if (stat(*directory_path, &st) != 0) {
        if (errno == ENOENT) {
            // directory does not exist, create it
            create_directories(*directory_path, full_path_size);
        }
    } else if (!S_ISDIR(st.st_mode)) {
        GGL_LOGE(
            "deployment-handler",
            "Path %s already exists, but is not a "
            "directory.",
            *directory_path
        );
        return GGL_ERR_INVALID;
    } else {
        GGL_LOGD(
            "deployment-handler",
            "Directory %s already exists.",
            *directory_path
        );
    }

    return GGL_ERR_OK;
}

static void create_directories(const char *path, size_t path_size) {
    char *tmp = malloc(path_size);
    tmp[0] = '\0';
    char *p = NULL;
    size_t len;

    snprintf(tmp, path_size, "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, S_IRWXU);
            *p = '/';
        }
    }
    mkdir(tmp, S_IRWXU);
    free(tmp);
}

static GglError copy_file(const char *src_path, const char *dest_path) {
    FILE *src = fopen(src_path, "rb");
    if (src == NULL) {
        GGL_LOGE(
            "deployment-handler", "Failed to open source file while copying."
        );
        return GGL_ERR_FAILURE;
    }

    FILE *dest = fopen(dest_path, "wb");
    if (dest == NULL) {
        GGL_LOGE(
            "deployment-handler",
            "Failed to open destination file while copying."
        );
        fclose(src);
        return GGL_ERR_FAILURE;
    }

    fseek(src, 0, SEEK_END);
    long file_size = ftell(src);
    fseek(src, 0, SEEK_SET);

    char *buff = malloc((size_t) file_size + 1);
    if (buff == NULL) {
        GGL_LOGE(
            "deployment-handler",
            "Failed to allocate memory to read recipe file."
        );
        fclose(src);
        fclose(dest);
        return GGL_ERR_FAILURE;
    }

    size_t bytes;
    while ((bytes = fread(buff, 1, sizeof(buff), src)) > 0) {
        if (fwrite(buff, 1, bytes, dest) != bytes) {
            GGL_LOGE(
                "deployment-handler", "Error writing to destination file."
            );
            fclose(src);
            fclose(dest);
            return GGL_ERR_FAILURE;
        }
    }

    fclose(src);
    fclose(dest);
    return GGL_ERR_OK;
}

static GglError copy_artifacts(GglBuffer artifact_dir, Recipe *recipe) {
    // NOTE: we currently do not support unzipping artifacts. This method
    // assumes all artifact files are available in the artifact directory path
    // provided in the deployment.
    (void) artifact_dir;

    // create the artifacts directory for the component
    char *directory_path = NULL;
    GglError create_directory_err
        = create_component_directory(recipe, &directory_path, ARTIFACTS);

    if (create_directory_err != GGL_ERR_OK) {
        free(directory_path);
        return create_directory_err;
    }

    // open and iterate through the provided artifacts directory
    struct dirent *entry;
    DIR *dir = opendir((char *) artifact_dir.data);

    if (dir == NULL) {
        GGL_LOGE(
            "deployment-handler",
            "Deployment document contains invalid artifact directory path."
        );
        free(directory_path);
        return GGL_ERR_INVALID;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    while ((entry = readdir(dir)) != NULL) {
        // check that the entry is not another directory
        if (entry->d_type != DT_DIR) {
            // build the full path to the file for both the source and
            // destination directories
            size_t filename_len = strlen(entry->d_name);
            size_t src_file_path_size
                = strlen((char *) artifact_dir.data) + filename_len + 10;
            size_t dest_file_path_size
                = strlen(directory_path) + filename_len + 10;

            char *src_file_path = malloc(src_file_path_size);
            src_file_path[0] = '\0';
            strncat(
                src_file_path, (char *) artifact_dir.data, artifact_dir.len
            );
            strncat(src_file_path, "/", 1);
            strncat(src_file_path, entry->d_name, filename_len);

            char *dest_file_path = malloc(dest_file_path_size);
            dest_file_path[0] = '\0';
            strncat(dest_file_path, directory_path, strlen(directory_path));
            strncat(dest_file_path, "/", 1);
            strncat(dest_file_path, entry->d_name, filename_len);

            GglError copy_file_err = copy_file(src_file_path, dest_file_path);

            if (copy_file_err != GGL_ERR_OK) {
                free(dest_file_path);
                free(src_file_path);
                free(directory_path);
                return copy_file_err;
            }

            free(src_file_path);
            free(dest_file_path);
        }
    }

    free(directory_path);
    return GGL_ERR_OK;
}
