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
