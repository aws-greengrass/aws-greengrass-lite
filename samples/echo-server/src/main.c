/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ggl/core_bus/server.h>
#include <ggl/object.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static void handle_echo(void *ctx, GglMap params, uint32_t handle) {
    (void) ctx;
    ggl_respond(handle, GGL_OBJ(params));
}

int main(void) {
    GglRpcMethodDesc handlers[] = {
        { GGL_STR("echo"), false, handle_echo, NULL },
    };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    ggl_listen(GGL_STR("/aws/ggl/echo-server"), handlers, handlers_len);
}
