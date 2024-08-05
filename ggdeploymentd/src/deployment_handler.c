// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "deployment_handler.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include "recipe_model.h"
#include <dirent.h>
#include <ggl/bump_alloc.h>
#include <ggl/error.h>
#include <ggl/json_decode.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void ggl_deployment_listen(void);
static void handle_deployment(GgdeploymentdDeployment);
static GglError load_recipe(GglBuffer recipe_dir);
static GglError load_artifact(GglBuffer artifact_dir);
static GglError read_recipe(char *recipe_path, Recipe *recipe);
static GglError parse_recipe(GglMap recipe_map, Recipe *recipe);

bool shutdown = false;

void *ggl_deployment_handler_start(void *ctx) {
    (void) ctx;
    ggl_deployment_listen();
    return NULL;
}

void ggl_deployment_handler_stop(void) {
    shutdown = true;
    // handle shutting down the thread
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
            if (deployment_doc.recipe_directory_path.len != 0) {
                load_recipe(deployment_doc.recipe_directory_path);
            }
            if (deployment_doc.artifact_directory_path.len != 0) {
                load_artifact(deployment_doc.artifact_directory_path);
            }
        }

        if (deployment.deployment_type == GGDEPLOYMENT_IOT_JOBS) {
            // not yet supported
        }

        if (deployment.deployment_type == GGDEPLOYMENT_SHADOW) {
            // not yet supported
        }
    }
}

static GglError load_recipe(GglBuffer recipe_dir) {
    // open and iterate through the provided recipe directory
    struct dirent *entry;
    DIR *dir = opendir((char *) recipe_dir.data);

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
        // note: d_type may not be available on all file systems, need to
        // research further
        if (entry->d_type != DT_DIR) {
            // build full path to recipe file
            size_t path_size = sizeof(recipe_dir.data) + sizeof(entry->d_name);
            char *full_path = malloc(path_size);
            snprintf(
                full_path,
                path_size,
                "%s/%s",
                (char *) recipe_dir.data,
                entry->d_name
            );
            Recipe recipe;
            GglError read_err = read_recipe(full_path, &recipe);

            if (read_err != GGL_ERR_OK) {
                free(full_path);
                return read_err;
            }

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
    static uint8_t
        json_decode_mem[GGL_RECIPE_CONTENT_MAX_SIZE * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(json_decode_mem));

    GglError decode_err
        = ggl_json_decode_destructive(recipe_content, &balloc.alloc, &val);
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

static GglError load_artifact(GglBuffer artifact_dir) {
    (void) artifact_dir;
    return GGL_ERR_OK;
}
