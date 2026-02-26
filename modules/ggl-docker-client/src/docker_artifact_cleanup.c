/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/list.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/docker_artifact_cleanup.h>
#include <ggl/docker_client.h>
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
    return (info.tag.len == 0) || gg_buffer_eq(info.tag, GG_STR("latest"));
}

/// returns whether two URIs refer to the same image
static bool docker_uri_equals(GglDockerUriInfo lhs, GglDockerUriInfo rhs) {
    if (!gg_buffer_eq(lhs.repository, rhs.repository)) {
        GG_LOGT(
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
        GG_LOGT("Comparing digests");
        return gg_buffer_eq(lhs.digest_algorithm, rhs.digest_algorithm)
            && gg_buffer_eq(lhs.digest, rhs.digest);
    }

    // Without digests, we can only make a best-guess effort.
    // Assumes that identical images won't be found on two
    // different registries (e.g. docker.io and public.ecr.aws)
    if (!gg_buffer_eq(lhs.registry, rhs.registry)) {
        GG_LOGT("Image tag from different registry");
        return false;
    }

    if (!gg_buffer_eq(lhs.username, rhs.username)) {
        GG_LOGT("Image from different user");
        return false;
    }

    if (gg_buffer_eq(lhs.tag, rhs.tag)) {
        GG_LOGT("Image tags match");
        return true;
    }

    if (is_tag_latest(lhs) && is_tag_latest(rhs)) {
        GG_LOGT("Images tag match");
        return true;
    }

    GG_LOGT("Image tags differ");

    return false;
}

static GgError docker_artifact_exists(
    int root_path_fd,
    GglDockerUriInfo image_uri,
    GgBuffer component_name,
    GgBuffer component_version,
    bool *exists
) {
    GG_LOGT(
        "Checking if %.*s-%.*s contains image",
        (int) component_name.len,
        component_name.data,
        (int) component_version.len,
        component_version.data
    );
    GG_MTX_SCOPE_GUARD(&recipe_mtx);
    GgArena recipe_arena = gg_arena_init(GG_BUF(recipe_buf));
    GgObject recipe_obj;

    GgError ret = ggl_recipe_get_from_file(
        root_path_fd,
        component_name,
        component_version,
        &recipe_arena,
        &recipe_obj
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (gg_obj_type(recipe_obj) != GG_TYPE_MAP) {
        return GG_ERR_PARSE;
    }

    GgList artifacts;
    ret = ggl_get_recipe_artifacts_for_platform(
        gg_obj_into_map(recipe_obj), &artifacts
    );
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LIST_FOREACH (artifact, artifacts) {
        if (gg_obj_type(*artifact) != GG_TYPE_MAP) {
            continue;
        }
        GgMap artifact_map = gg_obj_into_map(*artifact);

        GgObject *uri_obj = NULL;
        if (!gg_map_get(artifact_map, GG_STR("Uri"), &uri_obj)) {
            continue;
        }
        if (gg_obj_type(*uri_obj) != GG_TYPE_BUF) {
            continue;
        }
        GgBuffer uri = gg_obj_into_buf(*uri_obj);

        if (!gg_buffer_remove_prefix(&uri, GG_STR("docker:"))) {
            continue;
        }

        GglDockerUriInfo artifact_uri;
        ret = gg_docker_uri_parse(uri, &artifact_uri);
        if (ret != GG_ERR_OK) {
            continue;
        }
        if (docker_uri_equals(image_uri, artifact_uri)) {
            *exists = true;
            return GG_ERR_OK;
        }
    }

    *exists = false;
    return GG_ERR_OK;
}

static GgError ggl_docker_remove_if_unused(
    int root_path_fd,
    GgBuffer image_name,
    GgBuffer component_name,
    GgBuffer component_version
) {
    GG_LOGT("Remove if unused");
    if (component_name.len == 0) {
        return GG_ERR_INVALID;
    }

    GgBuffer component_list_memory = GG_BUF((uint8_t[4096]) { 0 });
    GgArena component_list_alloc = gg_arena_init(component_list_memory);

    GgList components;
    GgError ret = ggl_gg_config_list(
        GG_BUF_LIST(GG_STR("services")), &component_list_alloc, &components
    );
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    ret = gg_list_type_check(components, GG_TYPE_BUF);
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }

    GglDockerUriInfo image_uri;
    ret = gg_docker_uri_parse(image_name, &image_uri);
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }

    GG_LIST_FOREACH (component, components) {
        GgBuffer other_component_name = gg_obj_into_buf(*component);
        GG_LOGT(
            "Checking %.*s for docker images",
            (int) other_component_name.len,
            other_component_name.data
        );
        GgArena version_alloc = gg_arena_init(GG_BUF((uint8_t[256]) { 0 }));
        GgBuffer other_component_version;
        ret = ggl_gg_config_read_str(
            GG_BUF_LIST(GG_STR("services"), component_name, GG_STR("version")),
            &version_alloc,
            &other_component_version
        );
        if (ret != GG_ERR_OK) {
            continue;
        }

        if (gg_buffer_eq(other_component_name, component_name)
            && gg_buffer_eq(other_component_version, component_version)) {
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
        if (ret != GG_ERR_OK) {
            return GG_ERR_FAILURE;
        }
        if (exists) {
            return GG_ERR_OK;
        }
    }

    return ggl_docker_remove(image_name);
}

/// Process the i'th artifact of the component
/// Keeps at most one component's recipe in memory.
static bool ggl_docker_artifact_cleanup_step(
    int root_path_fd,
    GgBuffer component_name,
    GgBuffer component_version,
    size_t i
) {
    static uint8_t image_name_buf[4096];
    GgArena image_arena = gg_arena_init(GG_BUF(image_name_buf));
    GgBuffer image_name;

    {
        GG_MTX_SCOPE_GUARD(&recipe_mtx);

        GgArena recipe_arena = gg_arena_init(GG_BUF(recipe_buf));
        GgObject recipe;
        GgError ret = ggl_recipe_get_from_file(
            root_path_fd,
            component_name,
            component_version,
            &recipe_arena,
            &recipe
        );
        if ((ret != GG_ERR_OK) || (gg_obj_type(recipe) != GG_TYPE_MAP)) {
            return false;
        }
        GgList artifacts;
        ret = ggl_get_recipe_artifacts_for_platform(
            gg_obj_into_map(recipe), &artifacts
        );
        if (ret != GG_ERR_OK) {
            GG_LOGT("Couldn't get recipe artifacts");
            return false;
        }

        if (artifacts.len <= i) {
            GG_LOGT("Reached end of artifacts (%zu <= %zu)", artifacts.len, i);
            return false;
        }

        GgObject *uri_obj = NULL;
        if (!gg_map_get(
                gg_obj_into_map(artifacts.items[i]), GG_STR("Uri"), &uri_obj
            )) {
            GG_LOGT("No URI");
            return true;
        }
        if (gg_obj_type(*uri_obj) != GG_TYPE_BUF) {
            GG_LOGT("URI not a buffer");
            return true;
        }

        image_name = gg_obj_into_buf(*uri_obj);
        if (!gg_buffer_remove_prefix(&image_name, GG_STR("docker:"))) {
            GG_LOGT("URI not docker");
            return true;
        }
        GG_LOGT(
            "Preparing to remove %.*s if it's unused",
            (int) image_name.len,
            image_name.data
        );

        ret = gg_arena_claim_buf(&image_name, &image_arena);
        if (ret != GG_ERR_OK) {
            return true;
        }
    }

    (void) ggl_docker_remove_if_unused(
        root_path_fd, image_name, component_name, component_version
    );
    return true;
}

void ggl_docker_artifact_cleanup(
    int root_path_fd, GgBuffer component_name, GgBuffer component_version
) {
    size_t i = 0;
    while (ggl_docker_artifact_cleanup_step(
        root_path_fd, component_name, component_version, i
    )) {
        GG_LOGT("Finished step %zu", i);
        ++i;
    }
}
