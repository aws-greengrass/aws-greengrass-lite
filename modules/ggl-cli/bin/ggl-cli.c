// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <assert.h>
#include <errno.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/core_bus/client.h>
#include <ggl/nucleus/init.h>
#include <limits.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_LOCAL_DEPLOYMENT_COMPONENTS 10

typedef struct {
    char *name;
    char *version;
} Component;

char *command = NULL;
char *recipe_dir = NULL;
char *artifacts_dir = NULL;
char *group_name = NULL;
static Component components[MAX_LOCAL_DEPLOYMENT_COMPONENTS];
int component_count = 0;
static char *remove_components[MAX_LOCAL_DEPLOYMENT_COMPONENTS];
int remove_count = 0;

static char doc[] = "ggl-cli -- Greengrass CLI for Nucleus Lite";

static struct argp_option opts[] = {
    { "recipe-dir", 'r', "path", 0, "Recipe directory to merge", 0 },
    { "artifacts-dir", 'a', "path", 0, "Artifacts directory to merge", 0 },
    { "add-component", 'c', "name=version", 0, "Component to add...", 0 },
    { "remove-component", 'd', "name", 0, "Component to remove...", 0 },
    { "group-name", 'g', "name", 0, "Thing group name for deployment", 0 },
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
    case 'g':
        group_name = arg;
        break;
    case 'd': {
        if (remove_count >= MAX_LOCAL_DEPLOYMENT_COMPONENTS) {
            GG_LOGE(
                "Maximum of %d components allowed per local deployment",
                MAX_LOCAL_DEPLOYMENT_COMPONENTS
            );
            return ARGP_ERR_UNKNOWN;
        }
        remove_components[remove_count] = arg;
        remove_count++;
        break;
    }
    case 'c': {
        if (component_count >= MAX_LOCAL_DEPLOYMENT_COMPONENTS) {
            GG_LOGE(
                "Maximum of %d components allowed per local deployment",
                MAX_LOCAL_DEPLOYMENT_COMPONENTS
            );
            return ARGP_ERR_UNKNOWN;
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

static int setup_paths(GgKVVec *args) {
    if (recipe_dir != NULL) {
        static char recipe_full_path_buf[PATH_MAX];
        char *path = realpath(recipe_dir, recipe_full_path_buf);
        if (path == NULL) {
            GG_LOGE(
                "Failed to expand recipe dir path (%s): %d.", recipe_dir, errno
            );
            return 1;
        }

        GgError ret = gg_kv_vec_push(
            args,
            gg_kv(
                GG_STR("recipe_directory_path"),
                gg_obj_buf(gg_buffer_from_null_term(path))
            )
        );
        if (ret != GG_ERR_OK) {
            assert(false);
            return 1;
        }
    }
    if (artifacts_dir != NULL) {
        static char artifacts_full_path_buf[PATH_MAX];
        char *path = realpath(artifacts_dir, artifacts_full_path_buf);
        if (path == NULL) {
            GG_LOGE(
                "Failed to expand artifacts dir path (%s): %d.",
                artifacts_dir,
                errno
            );
            return 1;
        }

        GgError ret = gg_kv_vec_push(
            args,
            gg_kv(
                GG_STR("artifacts_directory_path"),
                gg_obj_buf(gg_buffer_from_null_term(path))
            )
        );
        if (ret != GG_ERR_OK) {
            assert(false);
            return 1;
        }
    }
    return 0;
}

static GgKV *setup_components(GgKVVec *args) {
    if (component_count == 0) {
        return NULL;
    }

    static GgKV pairs[MAX_LOCAL_DEPLOYMENT_COMPONENTS];
    GgKVVec component_pairs = { .map = { .pairs = pairs, .len = 0 },
                                .capacity = MAX_LOCAL_DEPLOYMENT_COMPONENTS };

    for (int i = 0; i < component_count; i++) {
        GgError ret = gg_kv_vec_push(
            &component_pairs,
            gg_kv(
                gg_buffer_from_null_term(components[i].name),
                gg_obj_buf(gg_buffer_from_null_term(components[i].version))
            )
        );
        if (ret != GG_ERR_OK) {
            assert(false);
            return NULL;
        }
    }

    GgError ret = gg_kv_vec_push(
        args,
        gg_kv(
            GG_STR("root_component_versions_to_add"),
            gg_obj_map(component_pairs.map)
        )
    );
    if (ret != GG_ERR_OK) {
        assert(false);
        return NULL;
    }

    GG_LOGI("Deploying %d components in a single deployment:", component_count);
    for (int i = 0; i < component_count; i++) {
        GG_LOGI("  - %s=%s", components[i].name, components[i].version);
    }
    return pairs;
}

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    ggl_nucleus_init();

    GgKVVec args = GG_KV_VEC((GgKV[5]) { 0 });

    if (setup_paths(&args) != 0) {
        return 1;
    }

    GgKV *pairs = setup_components(&args);
    if (component_count > 0 && pairs == NULL) {
        return 1;
    }

    if (group_name != NULL) {
        GgError ret = gg_kv_vec_push(
            &args,
            gg_kv(
                GG_STR("group_name"),
                gg_obj_buf(gg_buffer_from_null_term(group_name))
            )
        );
        if (ret != GG_ERR_OK) {
            assert(false);
            return 1;
        }
    }

    static GgObject remove_items[MAX_LOCAL_DEPLOYMENT_COMPONENTS];
    if (remove_count > 0) {
        for (int i = 0; i < remove_count; i++) {
            remove_items[i]
                = gg_obj_buf(gg_buffer_from_null_term(remove_components[i]));
        }
        GgError ret = gg_kv_vec_push(
            &args,
            gg_kv(
                GG_STR("root_component_versions_to_remove"),
                gg_obj_list((GgList) { .items = remove_items,
                                       .len = (size_t) remove_count })
            )
        );
        if (ret != GG_ERR_OK) {
            assert(false);
            return 1;
        }
    }

    GgError remote_err = GG_ERR_OK;
    static uint8_t buffer[8192];
    GgBuffer id_mem = { .data = buffer, .len = sizeof(buffer) };
    GgArena alloc = gg_arena_init(id_mem);
    GgObject result;

    GgError ret = ggl_call(
        GG_STR("gg_deployment"),
        GG_STR("create_local_deployment"),
        args.map,
        &remote_err,
        &alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        if (ret == GG_ERR_REMOTE) {
            GG_LOGE("Got error from deployment: %d.", remote_err);
        } else {
            GG_LOGE("Error sending deployment: %d.", ret);
        }
        return 1;
    }

    if (gg_obj_type(result) != GG_TYPE_BUF) {
        GG_LOGE("Invalid return type.");
        return 1;
    }

    GgBuffer result_buf = gg_obj_into_buf(result);

    GG_LOGI("Deployment id: %.*s.", (int) result_buf.len, result_buf.data);
    return 0;
}
