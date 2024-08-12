// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// tesd -- Token Exchange Service for AWS credential desperse management

#include "fleet-provisioning.h"
#include <ggl/error.h>

int main(void) {
    GglError ret = run_fleet_prov();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
