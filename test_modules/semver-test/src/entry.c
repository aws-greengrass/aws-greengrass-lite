// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include "ggl/semver.h"
#include "semver-test.h"
#include "stdbool.h"
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>

GgError run_semver_test(void) {
    bool ret = is_in_range(GG_STR("1.1.0"), GG_STR(">=2.1.0"));
    if (ret) {
        GG_LOGI("Satisfies requirement/s");
    } else {
        GG_LOGI("Does not satisfy requirement/s");
    }
    return GG_ERR_OK;
}
