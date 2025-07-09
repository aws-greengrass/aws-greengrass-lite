/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GGL_DOCKER_ARTIFACT_CLEANUP_H
#define GGL_DOCKER_ARTIFACT_CLEANUP_H

#include <ggl/buffer.h>

void ggl_docker_artifact_cleanup(
    int root_path_fd, GglBuffer component_name, GglBuffer component_version
);

#endif
