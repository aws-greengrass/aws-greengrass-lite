/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/docker_artifact_cleanup.h"
#include "ggl/core_bus/gg_config.h"
#include "ggl/docker_client.h"
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/list.h>
#include <ggl/log.h>
#include <ggl/map.h>
#include <ggl/object.h>
#include <ggl/recipe.h>
#include <ggl/uri.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>

static uint8_t recipe_buf[8192];
static pthread_mutex_t recipe_mtx = PTHREAD_MUTEX_INITIALIZER;

/// Assumes info does not contain a digest
static bool is_tag_latest(GglDockerUriInfo info) {
    return (info.tag.len == 0) || ggl_buffer_eq(info.tag, GGL_STR("latest"));
}

/// returns whether two URIs refer to the same image
static bool docker_uri_equals(GglDockerUriInfo lhs, GglDockerUriInfo rhs) {
    if (!ggl_buffer_eq(lhs.repository, rhs.repository)) {
        GGL_LOGT(
            "Image repository differs ([%.*s] != [%.*s])",
            (int) lhs.repository.len,
            lhs.repository.data,
            (int) rhs.repository.len,
            rhs.repository.data
        );
        return false;
    }

    // Comparing digests works regardless of where both images are sourced from.
    if ((lhs.digest.len > 0) || (rhs.digest.len > 0)) {
        GGL_LOGT("Comparing digests");
        return ggl_buffer_eq(lhs.digest_algorithm, rhs.digest_algorithm)
            && ggl_buffer_eq(lhs.digest, rhs.digest);
    }

    // Without digests, we can only make a best-guess effort.
    // Assumes that identical images won't be found on two
    // different registries (e.g. docker.io and public.ecr.aws)
    if (!ggl_buffer_eq(lhs.registry, rhs.registry)) {
        GGL_LOGT("Image tag from different registry");
        return false;
    }

    if (!ggl_buffer_eq(lhs.username, rhs.username)) {
        GGL_LOGT("Image from different user");
        return false;
    }

    if (ggl_buffer_eq(lhs.tag, rhs.tag)) {
        GGL_LOGT("Image tags match");
        return true;
    }

    if (is_tag_latest(lhs) && is_tag_latest(rhs)) {
        GGL_LOGT("Images tag match");
        return true;
    }

    GGL_LOGT("Image tags differ");

    return false;
}

static GglError docker_artifact_exists(
    int root_path_fd,
    GglDockerUriInfo image_uri,
    GglBuffer component_name,
    GglBuffer component_version,
    bool *exists
) {
    GGL_LOGT(
        "Checking if %.*s-%.*s contains image",
        (int) component_name.len,
        component_name.data,
        (int) component_version.len,
        component_version.data
    );
    GGL_MTX_SCOPE_GUARD(&recipe_mtx);
    GglArena recipe_arena = ggl_arena_init(GGL_BUF(recipe_buf));
    GglObject recipe_obj;

    GglError ret = ggl_recipe_get_from_file(
        root_path_fd,
        component_name,
        component_version,
        &recipe_arena,
        &recipe_obj
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    if (ggl_obj_type(recipe_obj) != GGL_TYPE_MAP) {
        return GGL_ERR_PARSE;
    }

    GglList artifacts;
    ret = ggl_get_recipe_artifacts_for_platform(
        ggl_obj_into_map(recipe_obj), &artifacts
    );
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    GGL_LIST_FOREACH (artifact, artifacts) {
        if (ggl_obj_type(*artifact) != GGL_TYPE_MAP) {
            continue;
        }
        GglMap artifact_map = ggl_obj_into_map(*artifact);

        GglObject *uri_obj = NULL;
        if (!ggl_map_get(artifact_map, GGL_STR("Uri"), &uri_obj)) {
            continue;
        }
        if (ggl_obj_type(*uri_obj) != GGL_TYPE_BUF) {
            continue;
        }
        GglBuffer uri = ggl_obj_into_buf(*uri_obj);

        if (!ggl_buffer_remove_prefix(&uri, GGL_STR("docker:"))) {
            continue;
        }

        GglDockerUriInfo artifact_uri;
        ret = gg_docker_uri_parse(uri, &artifact_uri);
        if (ret != GGL_ERR_OK) {
            continue;
        }
        if (docker_uri_equals(image_uri, artifact_uri)) {
            *exists = true;
            return GGL_ERR_OK;
        }
    }

    *exists = false;
    return GGL_ERR_OK;
}

static GglError ggl_docker_remove_if_unused(
    int root_path_fd,
    GglBuffer image_name,
    GglBuffer component_name,
    GglBuffer component_version
) {
    GGL_LOGT("Remove if unused");
    if (component_name.len == 0) {
        return GGL_ERR_INVALID;
    }

    GglBuffer component_list_memory = GGL_BUF((uint8_t[4096]) { 0 });
    GglArena component_list_alloc = ggl_arena_init(component_list_memory);

    GglList components;
    GglError ret = ggl_gg_config_list(
        GGL_BUF_LIST(GGL_STR("services")), &component_list_alloc, &components
    );
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    ret = ggl_list_type_check(components, GGL_TYPE_BUF);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    GglDockerUriInfo image_uri;
    ret = gg_docker_uri_parse(image_name, &image_uri);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }

    GGL_LIST_FOREACH (component, components) {
        GglBuffer other_component_name = ggl_obj_into_buf(*component);
        GGL_LOGT(
            "Checking %.*s for docker images",
            (int) other_component_name.len,
            other_component_name.data
        );
        GglArena version_alloc = ggl_arena_init(GGL_BUF((uint8_t[256]) { 0 }));
        GglBuffer other_component_version;
        ret = ggl_gg_config_read_str(
            GGL_BUF_LIST(
                GGL_STR("services"), component_name, GGL_STR("version")
            ),
            &version_alloc,
            &other_component_version
        );
        if (ret != GGL_ERR_OK) {
            continue;
        }

        if (ggl_buffer_eq(other_component_name, component_name)
            && ggl_buffer_eq(other_component_version, component_version)) {
            continue;
        }

        bool exists = false;

        ret = docker_artifact_exists(
            root_path_fd,
            image_uri,
            other_component_name,
            other_component_version,
            &exists
        );
        if (ret != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }
        if (exists) {
            return GGL_ERR_OK;
        }
    }

    return ggl_docker_remove(image_name);
}

/// Process the i'th artifact of the component
/// Keeps at most one component's recipe in memory.
static bool ggl_docker_artifact_cleanup_step(
    int root_path_fd,
    GglBuffer component_name,
    GglBuffer component_version,
    size_t i
) {
    static uint8_t image_name_buf[4096];
    GglArena image_arena = ggl_arena_init(GGL_BUF(image_name_buf));
    GglBuffer image_name;

    {
        GGL_MTX_SCOPE_GUARD(&recipe_mtx);

        GglArena recipe_arena = ggl_arena_init(GGL_BUF(recipe_buf));
        GglObject recipe;
        GglError ret = ggl_recipe_get_from_file(
            root_path_fd,
            component_name,
            component_version,
            &recipe_arena,
            &recipe
        );
        if ((ret != GGL_ERR_OK) || (ggl_obj_type(recipe) != GGL_TYPE_MAP)) {
            return false;
        }
        GglList artifacts;
        ret = ggl_get_recipe_artifacts_for_platform(
            ggl_obj_into_map(recipe), &artifacts
        );
        if (ret != GGL_ERR_OK) {
            GGL_LOGT("Couldn't get recipe artifacts");
            return false;
        }

        if (artifacts.len <= i) {
            GGL_LOGT("Reached end of artifacts (%zu <= %zu)", artifacts.len, i);
            return false;
        }

        GglObject *uri_obj = NULL;
        if (!ggl_map_get(
                ggl_obj_into_map(artifacts.items[i]), GGL_STR("Uri"), &uri_obj
            )) {
            GGL_LOGT("No URI");
            return true;
        }
        if (ggl_obj_type(*uri_obj) != GGL_TYPE_BUF) {
            GGL_LOGT("URI not a buffer");
            return true;
        }

        image_name = ggl_obj_into_buf(*uri_obj);
        if (!ggl_buffer_remove_prefix(&image_name, GGL_STR("docker:"))) {
            GGL_LOGT("URI not docker");
            return true;
        }
        GGL_LOGT(
            "Preparing to remove %.*s if it's unused",
            (int) image_name.len,
            image_name.data
        );

        ret = ggl_arena_claim_buf(&image_name, &image_arena);
        if (ret != GGL_ERR_OK) {
            return true;
        }
    }

    (void) ggl_docker_remove_if_unused(
        root_path_fd, image_name, component_name, component_version
    );
    return true;
}

void ggl_docker_artifact_cleanup(
    int root_path_fd, GglBuffer component_name, GglBuffer component_version
) {
    size_t i = 0;
    while (ggl_docker_artifact_cleanup_step(
        root_path_fd, component_name, component_version, i
    )) {
        GGL_LOGT("Finished step %zu", i);
        ++i;
    }
}
