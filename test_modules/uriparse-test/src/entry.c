// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "uriparse-test.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/attr.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/uri.h>
#include <stdbool.h>

static GglError docker_test(GglBuffer docker_uri, const GglUriInfo *expected)
    NONNULL(2);

static GglError docker_test(GglBuffer docker_uri, const GglUriInfo *expected) {
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
            "image:sha256@"
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
        )
    );
    const GglUriInfo EXPECTED_OUTPUT[] = {
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
                      "image:sha256@"
                      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b"
                      "7852b855") }
    };

    assert(
        sizeof(EXPECTED_OUTPUT) / sizeof(*EXPECTED_OUTPUT)
        == DOCKER_ECR_URIS.len
    );

    for (size_t i = 0; i < DOCKER_ECR_URIS.len; ++i) {
        if (docker_test(DOCKER_ECR_URIS.bufs[i], &EXPECTED_OUTPUT[i])
            != GGL_ERR_OK) {
            return GGL_ERR_FAILURE;
        }
    }
    return GGL_ERR_OK;
}
