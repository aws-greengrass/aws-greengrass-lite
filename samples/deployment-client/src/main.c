// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <errno.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/client.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stddef.h>
#include <stdint.h>

int main(void) {
    GglBuffer server = GGL_STR("/aws/ggl/ggdeploymentd");
    static uint8_t buffer[10 * sizeof(GglObject)] = { 0 };

    GglMap args = GGL_MAP({ GGL_STR("recipe_directory_path"), GGL_OBJ_STR("aaa") });

    GglBumpAlloc alloc = ggl_bump_alloc_init(GGL_BUF(buffer));
    GglObject result;

    GglError ret = ggl_call(
        server, GGL_STR("create_local_deployment"), args, NULL, &alloc.alloc, &result
    );

    if (ret != 0) {
        GGL_LOGE("deployment-client", "Failed to send create_local_deployment: %d.", ret);
        return EPROTO;
    }
}
