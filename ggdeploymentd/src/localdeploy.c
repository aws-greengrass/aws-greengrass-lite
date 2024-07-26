/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "localdeploy.h"
#include "args.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include "ggl/utils.h"
#include <assert.h>


GglError ggdeploymentd_create_local_deployment(const GgdeploymentdLocalDeployment *local_deployment) {
    assert(local_deployment != NULL);

    GGL_LOGD(
            "create_local_deployment",
            "Received a local deployment from core bus."
    );



    return 0;
}

void ggl_receive_callback(
    void *ctx, GglBuffer method, GglList params, GglResponseHandle *handle
) {
    (void) ctx;

    if ((params.len < 1) || (params.items[0].type != GGL_TYPE_MAP)) {
        GGL_LOGE("ggdeploymentd-corebus", "Received invalid arguments.");
        ggl_respond(handle, GGL_ERR_INVALID, GGL_OBJ_NULL());
        return;
    }
}