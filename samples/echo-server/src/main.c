/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggl/buffer.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include <stdlib.h>

void ggl_receive_callback(
    void *ctx, GglBuffer method, GglList params, GglResponseHandle *handle
) {
    (void) ctx;

    if ((params.len < 1) && (params.items[0].type != GGL_TYPE_MAP)) {
        GGL_LOGE("rpc-handler", "Publish received invalid arguments.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }

    GglMap param_map = params.items[0].map;

    if (ggl_buffer_eq(method, GGL_STR("echo"))) {
        ggl_respond(handle, 0, GGL_OBJ(param_map));
        return;
    }

    ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
}

int main(void) {
    ggl_listen(GGL_STR("/aws/ggl/echo-server"), NULL);
}
