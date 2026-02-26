// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "bus_server.h"
#include "deployment_model.h"
#include "deployment_queue.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/core_bus/server.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static GgError create_local_deployment(
    void *ctx, GgMap params, uint32_t handle
) {
    (void) ctx;

    GG_LOGT("Received create_local_deployment from core bus.");

    GgByteVec id = GG_BYTE_VEC((uint8_t[36]) { 0 });

    GgError ret = ggl_deployment_enqueue(params, &id, LOCAL_DEPLOYMENT);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ggl_respond(handle, gg_obj_buf(id.buf));
    return GG_ERR_OK;
}

void ggdeploymentd_start_server(void) {
    GG_LOGI("Starting ggdeploymentd core bus server.");

    GglRpcMethodDesc handlers[] = { { GG_STR("create_local_deployment"),
                                      false,
                                      create_local_deployment,
                                      NULL } };
    size_t handlers_len = sizeof(handlers) / sizeof(handlers[0]);

    GgError ret = ggl_listen(GG_STR("gg_deployment"), handlers, handlers_len);

    GG_LOGE("Exiting with error %u.", (unsigned) ret);
}
