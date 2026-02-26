// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/uri.h>
#include <uriparse-test.h>
#include <stddef.h>
#include <stdint.h>

#define TEST_NULL_BUF ((GgBuffer) { .data = NULL, .len = 0 })

static GgError docker_test(
    GgBuffer docker_uri,
    const GglUriInfo expected[static 1],
    const GglDockerUriInfo expected_docker[static 1]
) {
    uint8_t test_buffer[256];
    GgArena parse_arena = gg_arena_init(GG_BUF(test_buffer));
    GglUriInfo info = { 0 };
    GgError ret = gg_uri_parse(&parse_arena, docker_uri, &info);
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(expected->scheme, info.scheme)) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(expected->path, info.path)) {
        return GG_ERR_FAILURE;
    }

    GglDockerUriInfo docker_info = { 0 };
    ret = gg_docker_uri_parse(info.path, &docker_info);
    if (ret != GG_ERR_OK) {
        return GG_ERR_FAILURE;
    }
    // [registry/][username/]repository[:tag|@digest]
    GG_LOGD(
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
    if (!gg_buffer_eq(expected_docker->digest, docker_info.digest)) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(
            expected_docker->digest_algorithm, docker_info.digest_algorithm
        )) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(expected_docker->tag, docker_info.tag)) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(expected_docker->registry, docker_info.registry)) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(expected_docker->repository, docker_info.repository)) {
        return GG_ERR_FAILURE;
    }
    if (!gg_buffer_eq(expected_docker->username, docker_info.username)) {
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

GgError run_uriparse_test(void) {
    const GgBufList DOCKER_ECR_URIS = GG_BUF_LIST(
        // Public ECR
        GG_STR("docker:public.ecr.aws/cloudwatch-agent/cloudwatch-agent:latest"
        ),
        // Dockerhub
        GG_STR("docker:mysql:8.0"),
        // Private ECR
        GG_STR(
            "docker:012345678901.dkr.ecr.region.amazonaws.com/repository/image:latest"
        ),
        // Private ECR w/ digest
        GG_STR(
            "docker:012345678901.dkr.ecr.region.amazonaws.com/repository/image@sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        )
    );
    const GglUriInfo EXPECTED_URI[] = {
        (GglUriInfo
        ) { .scheme = GG_STR("docker"),
            .path
            = GG_STR("public.ecr.aws/cloudwatch-agent/cloudwatch-agent:latest"
            ) },
        (GglUriInfo) { .scheme = GG_STR("docker"),
                       .path = GG_STR("mysql:8.0") },
        (GglUriInfo
        ) { .scheme = GG_STR("docker"),
            .path = GG_STR(
                "012345678901.dkr.ecr.region.amazonaws.com/repository/image:latest"
            ) },
        (GglUriInfo
        ) { .scheme = GG_STR("docker"),
            .path = GG_STR(
                "012345678901.dkr.ecr.region.amazonaws.com/repository/image@sha256:e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            ) }
    };

    const GglDockerUriInfo EXPECTED_DOCKER_URI[] = {
        (GglDockerUriInfo) { .registry = GG_STR("public.ecr.aws"),
                             .username = GG_STR("cloudwatch-agent"),
                             .repository = GG_STR("cloudwatch-agent"),
                             .tag = GG_STR("latest") },
        (GglDockerUriInfo) { .registry = GG_STR("docker.io"),
                             .repository = GG_STR("mysql"),
                             .tag = GG_STR("8.0") },
        (GglDockerUriInfo
        ) { .registry = GG_STR("012345678901.dkr.ecr.region.amazonaws.com"),
            .username = GG_STR("repository"),
            .repository = GG_STR("image"),
            .tag = GG_STR("latest") },
        (GglDockerUriInfo
        ) { .registry = GG_STR("012345678901.dkr.ecr.region.amazonaws.com"),
            .username = GG_STR("repository"),
            .repository = GG_STR("image"),
            .digest_algorithm = GG_STR("sha256"),
            .digest = GG_STR(
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
            ) }
    };
    static_assert(
        sizeof(EXPECTED_DOCKER_URI) / sizeof(*EXPECTED_DOCKER_URI)
            == sizeof(EXPECTED_URI) / sizeof(*EXPECTED_URI),
        "Test case input/output should match"
    );

    assert(sizeof(EXPECTED_URI) / sizeof(*EXPECTED_URI) == DOCKER_ECR_URIS.len);

    GgError ret = GG_ERR_OK;
    for (size_t i = 0; i < DOCKER_ECR_URIS.len; ++i) {
        if (docker_test(
                DOCKER_ECR_URIS.bufs[i],
                &EXPECTED_URI[i],
                &EXPECTED_DOCKER_URI[i]
            )
            != GG_ERR_OK) {
            ret = GG_ERR_FAILURE;
        }
    }
    return ret;
}
