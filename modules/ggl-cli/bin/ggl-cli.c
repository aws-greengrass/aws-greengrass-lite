// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <assert.h>
#include <errno.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/nucleus/init.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct {
    char *name;
    char *version;
} Component;

char *command = NULL;
char *recipe_dir = NULL;
char *artifacts_dir = NULL;
Component *components = NULL;
int component_count = 0;

static char doc[] = "ggl-cli -- Greengrass CLI for Nucleus Lite";

static struct argp_option opts[] = {
    { "recipe-dir", 'r', "path", 0, "Recipe directory to merge", 0 },
    { "artifacts-dir", 'a', "path", 0, "Artifacts directory to merge", 0 },
    { "add-component", 'c', "name=version", 0, "Component to add...", 0 },
    { 0 },
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    (void) arg;
    switch (key) {
    case 'r':
        recipe_dir = arg;
        break;
    case 'a':
        artifacts_dir = arg;
        break;
    case 'c': {
        Component *new_components = realloc(
            components, (size_t) (component_count + 1) * sizeof(Component)
        );
        if (new_components == NULL) {
            GGL_LOGE("Memory allocation failed for components");
            return ARGP_ERR_UNKNOWN;
        }
        components = new_components;
        char *eq = strchr(arg, '=');
        if (eq == NULL) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            argp_usage(state);
            break;
        }
        *eq = '\0';
        components[component_count].name = arg;
        components[component_count].version = &eq[1];
        component_count++;
        break;
    }
    case ARGP_KEY_ARG:
        if (command != NULL) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            argp_usage(state);
        }
        if (strcmp(arg, "deploy") == 0) {
            command = arg;
            break;
        }
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        argp_usage(state);
        break;
    case ARGP_KEY_NO_ARGS:
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        argp_usage(state);
        break;
    default:
        break;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, "deploy", doc, 0, 0, 0 };

static int setup_paths(GglKVVec *args) {
    if (recipe_dir != NULL) {
        static char recipe_full_path_buf[PATH_MAX];
        char *path = realpath(recipe_dir, recipe_full_path_buf);
        if (path == NULL) {
            GGL_LOGE(
                "Failed to expand recipe dir path (%s): %d.", recipe_dir, errno
            );
            return 1;
        }

        GglError ret = ggl_kv_vec_push(
            args,
            ggl_kv(
                GGL_STR("recipe_directory_path"),
                ggl_obj_buf(ggl_buffer_from_null_term(path))
            )
        );
        if (ret != GGL_ERR_OK) {
            assert(false);
            return 1;
        }
    }
    if (artifacts_dir != NULL) {
        static char artifacts_full_path_buf[PATH_MAX];
        char *path = realpath(artifacts_dir, artifacts_full_path_buf);
        if (path == NULL) {
            GGL_LOGE(
                "Failed to expand artifacts dir path (%s): %d.",
                artifacts_dir,
                errno
            );
            return 1;
        }

        GglError ret = ggl_kv_vec_push(
            args,
            ggl_kv(
                GGL_STR("artifacts_directory_path"),
                ggl_obj_buf(ggl_buffer_from_null_term(path))
            )
        );
        if (ret != GGL_ERR_OK) {
            assert(false);
            return 1;
        }
    }
    return 0;
}

static GglKV *setup_components(GglKVVec *args) {
    if (component_count == 0) {
        return NULL;
    }

    GglKV *pairs = malloc((size_t) component_count * sizeof(GglKV));
    if (pairs == NULL) {
        GGL_LOGE("Memory allocation failed for component pairs");
        return NULL;
    }
    GglKVVec component_pairs = { .map = { .pairs = pairs, .len = 0 },
                                 .capacity = (size_t) component_count };

    for (int i = 0; i < component_count; i++) {
        GglError ret = ggl_kv_vec_push(
            &component_pairs,
            ggl_kv(
                ggl_buffer_from_null_term(components[i].name),
                ggl_obj_buf(ggl_buffer_from_null_term(components[i].version))
            )
        );
        if (ret != GGL_ERR_OK) {
            free(pairs);
            assert(false);
            return NULL;
        }
    }

    GglError ret = ggl_kv_vec_push(
        args,
        ggl_kv(
            GGL_STR("root_component_versions_to_add"),
            ggl_obj_map(component_pairs.map)
        )
    );
    if (ret != GGL_ERR_OK) {
        free(pairs);
        assert(false);
        return NULL;
    }

    GGL_LOGI(
        "Deploying %d components in a single deployment:", component_count
    );
    for (int i = 0; i < component_count; i++) {
        GGL_LOGI("  - %s=%s", components[i].name, components[i].version);
    }
    return pairs;
}

static void cleanup_memory(GglKV *pairs) {
    if (pairs != NULL) {
        free(pairs);
    }
    if (components != NULL) {
        free(components);
    }
}

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    ggl_nucleus_init();

    GglKVVec args = GGL_KV_VEC((GglKV[3]) { 0 });

    if (setup_paths(&args) != 0) {
        cleanup_memory(NULL);
        return 1;
    }

    GglKV *pairs = setup_components(&args);
    if (component_count > 0 && pairs == NULL) {
        cleanup_memory(NULL);
        return 1;
    }

    GglError remote_err = GGL_ERR_OK;
    static uint8_t buffer[8192];
    GglBuffer id_mem = { .data = buffer, .len = sizeof(buffer) };
    GglArena alloc = ggl_arena_init(id_mem);
    GglObject result;

    GglError ret = ggl_call(
        GGL_STR("gg_deployment"),
        GGL_STR("create_local_deployment"),
        args.map,
        &remote_err,
        &alloc,
        &result
    );
    if (ret != GGL_ERR_OK) {
        if (ret == GGL_ERR_REMOTE) {
            GGL_LOGE("Got error from deployment: %d.", remote_err);
        } else {
            GGL_LOGE("Error sending deployment: %d.", ret);
        }
        cleanup_memory(pairs);
        return 1;
    }

    if (ggl_obj_type(result) != GGL_TYPE_BUF) {
        GGL_LOGE("Invalid return type.");
        cleanup_memory(pairs);
        return 1;
    }

    GglBuffer result_buf = ggl_obj_into_buf(result);

    GGL_LOGI("Deployment id: %.*s.", (int) result_buf.len, result_buf.data);
    cleanup_memory(pairs);
    return 0;
}
