// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <ggl/core_bus/server.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GglError create_local_deployment(
    void *ctx, GglMap params, uint32_t handle
) {
    (void) ctx;

    GGL_LOGT("Received create_local_deployment from core bus.");

    GglByteVec id = GGL_BYTE_VEC((uint8_t[36]) { 0 });

    GglError ret = ggl_deployment_enqueue(params, &id, LOCAL_DEPLOYMENT);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, GGL_OBJ_BUF(id.buf));
    return GGL_ERR_OK;
}

void ggdeploymentd_start_server(void) {
    GGL_LOGI("Starting ggdeploymentd core bus server.");

    GglRpcMethodDesc handlers[] = { { GGL_STR("create_local_deployment"),
                                      false,
                                      create_local_deployment,
                                      NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GglError ret = ggl_listen(GGL_STR("gg_deployment"), handlers, handlers_len);

    GGL_LOGE("Exiting with error %u.", (unsigned) ret);
}
