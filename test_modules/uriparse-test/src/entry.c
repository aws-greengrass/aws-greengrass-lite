// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "uriparse-test.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/uri.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_NULL_BUF ((GglBuffer) { .data = NULL, .len = 0 })

static GglError docker_test(
    GglBuffer docker_uri,
    const GglUriInfo expected[static 1],
    const GglDockerUriInfo expected_docker[static 1]
) {
    uint8_t test_buffer[256];
    GglArena parse_arena = ggl_arena_init(GGL_BUF(test_buffer));
    GglUriInfo info = { 0 };
    GglError ret = gg_uri_parse(&parse_arena, docker_uri, &info);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(expected->scheme, info.scheme)) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(expected->path, info.path)) {
        return GGL_ERR_FAILURE;
    }

    GglDockerUriInfo docker_info = { 0 };
    ret = gg_docker_uri_parse(info.path, &docker_info);
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_FAILURE;
    }
    // [registry/][username/]repository[:tag|@digest]
    GGL_LOGD(
        " URI: %.*s%s%.*s%s%.*s%s%.*s%s%.*s",
        (int) docker_info.registry.len,
        docker_info.registry.data,
        docker_info.registry.len > 0 ? "/" : "",
        (int) docker_info.username.len,
        docker_info.username.data,
        docker_info.username.len > 0 ? "/" : "",
        (int) docker_info.repository.len,
        docker_info.repository.data,
        docker_info.tag.len > 0                    ? ":"
            : docker_info.digest_algorithm.len > 0 ? "@"
                                                   : "",
        docker_info.tag.len > 0 ? (int) docker_info.tag.len
                                : (int) docker_info.digest_algorithm.len,
        docker_info.tag.len > 0 ? docker_info.tag.data
                                : docker_info.digest_algorithm.data,
        docker_info.digest_algorithm.len > 0 ? ":" : "",
        (int) docker_info.digest.len,
        docker_info.digest.data
    );
    if (!ggl_buffer_eq(expected_docker->digest, docker_info.digest)) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(
            expected_docker->digest_algorithm, docker_info.digest_algorithm
        )) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(expected_docker->tag, docker_info.tag)) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(expected_docker->registry, docker_info.registry)) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(expected_docker->repository, docker_info.repository)) {
        return GGL_ERR_FAILURE;
    }
    if (!ggl_buffer_eq(expected_docker->username, docker_info.username)) {
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

GglError run_uriparse_test(void) {
    const GglBufList DOCKER_ECR_URIS = GGL_BUF_LIST(
        // Public ECR
        GGL_STR("docker:public.ecr.aws/cloudwatch-agent/cloudwatch-agent:latest"
        ),
        // Dockerhub
        GGL_STR("docker:mysql:8.0"),
        // Private ECR
        GGL_STR("docker:012345678901.dkr.ecr.region.amazonaws.com/repository/"
                "image:latest"),
        // Private ECR w/ digest
        GGL_STR(
            "docker:012345678901.dkr.ecr.region.amazonaws.com/repository/"
            "image@sha256:"
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        )
    );
    const GglUriInfo EXPECTED_URI[] = {
        (GglUriInfo
        ) { .scheme = GGL_STR("docker"),
            .path
            = GGL_STR("public.ecr.aws/cloudwatch-agent/cloudwatch-agent:latest"
            ) },
        (GglUriInfo) { .scheme = GGL_STR("docker"),
                       .path = GGL_STR("mysql:8.0") },
        (GglUriInfo
        ) { .scheme = GGL_STR("docker"),
            .path
            = GGL_STR("012345678901.dkr.ecr.region.amazonaws.com/repository/"
                      "image:latest") },
        (GglUriInfo
        ) { .scheme = GGL_STR("docker"),
            .path
            = GGL_STR("012345678901.dkr.ecr.region.amazonaws.com/repository/"
                      "image@sha256:"
                      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b"
                      "7852b855") }
    };

    const GglDockerUriInfo EXPECTED_DOCKER_URI[] = {
        (GglDockerUriInfo) { .registry = GGL_STR("public.ecr.aws"),
                             .username = GGL_STR("cloudwatch-agent"),
                             .repository = GGL_STR("cloudwatch-agent"),
                             .tag = GGL_STR("latest") },
        (GglDockerUriInfo) { .registry = GGL_STR("docker.io"),
                             .repository = GGL_STR("mysql"),
                             .tag = GGL_STR("8.0") },
        (GglDockerUriInfo
        ) { .registry = GGL_STR("012345678901.dkr.ecr.region.amazonaws.com"),
            .username = GGL_STR("repository"),
            .repository = GGL_STR("image"),
            .tag = GGL_STR("latest") },
        (GglDockerUriInfo
        ) { .registry = GGL_STR("012345678901.dkr.ecr.region.amazonaws.com"),
            .username = GGL_STR("repository"),
            .repository = GGL_STR("image"),
            .digest_algorithm = GGL_STR("sha256"),
            .digest
            = GGL_STR("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b"
                      "7852b855") }
    };
    static_assert(
        sizeof(EXPECTED_DOCKER_URI) / sizeof(*EXPECTED_DOCKER_URI)
            == sizeof(EXPECTED_URI) / sizeof(*EXPECTED_URI),
        "Test case input/output should match"
    );

    assert(sizeof(EXPECTED_URI) / sizeof(*EXPECTED_URI) == DOCKER_ECR_URIS.len);

    GglError ret = GGL_ERR_OK;
    for (size_t i = 0; i < DOCKER_ECR_URIS.len; ++i) {
        if (docker_test(
                DOCKER_ECR_URIS.bufs[i],
                &EXPECTED_URI[i],
                &EXPECTED_DOCKER_URI[i]
            )
            != GGL_ERR_OK) {
            ret = GGL_ERR_FAILURE;
        }
    }
    return ret;
}
