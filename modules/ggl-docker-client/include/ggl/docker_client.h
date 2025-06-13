/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_DOCKER_CLIENT_H
#define GGL_DOCKER_CLIENT_H

#include <ggl/buffer.h>
#include <ggl/error.h>

GglError ggl_docker_check_server(void);
GglError ggl_docker_pull(GglBuffer image_name);
GglError ggl_docker_remove(GglBuffer image_name);
GglError ggl_docker_check_image(GglBuffer image_name);
GglError ggl_docker_credentials_store(
    GglBuffer registry, GglBuffer username, GglBuffer secret
);

typedef struct SigV4Details SigV4Details;
GglError ggl_docker_credentials_ecr_retrieve(
    GglBuffer ecr_registry, struct SigV4Details sigv4_details
);

#endif
