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
#include <stdio.h>
#include <stdlib.h>

#define MAX_COMPONENTS 10

typedef struct {
    char *name;
    char *version;
} Component;

char *command = NULL;
char *recipe_dir = NULL;
char *artifacts_dir = NULL;
Component components[MAX_COMPONENTS];
int component_count = 0;

static char doc[] = "ggl-cli -- Greengrass CLI for Nucleus Lite";

static struct argp_option opts[] = {
    { "recipe-dir", 'r', "path", 0, "Recipe directory to merge", 0 },
    { "artifacts-dir", 'a', "path", 0, "Artifacts directory to merge", 0 },
    { "add-component", 'c', "name=version", 0, "Component to add (can be used multiple times)", 0 },
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
        if (component_count >= MAX_COMPONENTS) {
            fprintf(stderr, "Error: Maximum %d components supported\n", MAX_COMPONENTS);
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            argp_usage(state);
            break;
        }
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

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    ggl_nucleus_init();

    GglKVVec args = GGL_KV_VEC((GglKV[3]) { 0 });

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
            &args,
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
            &args,
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

    // Handle multiple components
    if (component_count > 0) {
        // Allocate memory for component pairs
        static GglKV component_pairs[MAX_COMPONENTS];

        for (int i = 0; i < component_count; i++) {
            component_pairs[i] = ggl_kv(
                ggl_buffer_from_null_term(components[i].name),
                ggl_obj_buf(ggl_buffer_from_null_term(components[i].version))
            );
        }

        GglError ret = ggl_kv_vec_push(
            &args,
            ggl_kv(
                GGL_STR("root_component_versions_to_add"),
                ggl_obj_map((GglMap) { .pairs = component_pairs, .len = component_count })
            )
        );
        if (ret != GGL_ERR_OK) {
            assert(false);
            return 1;
        }

        printf("Deploying %d components in a single deployment:\n", component_count);
        for (int i = 0; i < component_count; i++) {
            printf("  - %s=%s\n", components[i].name, components[i].version);
        }
    }

    GglError remote_err = GGL_ERR_OK;
    GglBuffer id_mem = GGL_BUF((uint8_t[36]) { 0 });
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
            return 1;
        }
        GGL_LOGE("Error sending deployment: %d.", ret);
        return 1;
    }

    if (ggl_obj_type(result) != GGL_TYPE_BUF) {
        GGL_LOGE("Invalid return type.");
        return 1;
    }

    GglBuffer result_buf = ggl_obj_into_buf(result);

    printf("Deployment id: %.*s.\n", (int) result_buf.len, result_buf.data);
    return 0;
}
