/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_DOCKER_CLIENT_H
#define GGL_DOCKER_CLIENT_H

#include <gg/error.h>
#include <gg/types.h>
#include <ggl/http.h>
#include <ggl/uri.h>
#include <stdbool.h>

GgError ggl_docker_check_server(void);
GgError ggl_docker_pull(GgBuffer image_name);
GgError ggl_docker_remove(GgBuffer image_name);
GgError ggl_docker_check_image(GgBuffer image_name);
GgError ggl_docker_credentials_store(
    GgBuffer registry, GgBuffer username, GgBuffer secret
);

/// Request credentials from ECR and pipe them to `docker login`
GgError ggl_docker_credentials_ecr_retrieve(
    GglDockerUriInfo ecr_registry, SigV4Details sigv4_details
);

bool ggl_docker_is_uri_private_ecr(GglDockerUriInfo docker_uri);

#endif
