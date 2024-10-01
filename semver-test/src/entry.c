// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/semver.h"
#include "semver-test.h"
#include "stdbool.h"
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>

GglError run_semver_test(void) {
    bool ret = is_contain(GGL_STR("1.1.0"), GGL_STR(">=2.1.0"));
    if (ret) {
        GGL_LOGI("SEMVER-test", "Satisfies requirement/s");
    } else {
        GGL_LOGI("SEMVER-test", "Does not satisfy requirement/s");
    }
    return GGL_ERR_OK;
}
