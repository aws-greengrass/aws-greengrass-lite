/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_DOCKER_CLIENT_H
#define GGL_DOCKER_CLIENT_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/http.h>
#include <ggl/uri.h>
#include <stdbool.h>

GglError ggl_docker_check_server(void);
GglError ggl_docker_pull(GglBuffer image_name);
GglError ggl_docker_remove(GglBuffer image_name);
GglError ggl_docker_check_image(GglBuffer image_name);
GglError ggl_docker_credentials_store(
    GglBuffer registry, GglBuffer username, GglBuffer secret
);

/// Request credentials from ECR and pipe them to `docker login`
GglError ggl_docker_credentials_ecr_retrieve(
    GglDockerUriInfo ecr_registry, SigV4Details sigv4_details
);

bool ggl_docker_is_uri_private_ecr(GglDockerUriInfo docker_uri);

#endif
