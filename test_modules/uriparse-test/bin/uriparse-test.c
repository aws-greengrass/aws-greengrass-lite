// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX - License - Identifier : Apache - 2.0

#include <gg/error.h>
#include <uriparse-test.h>

int main(void) {
    GgError ret = run_uriparse_test();
    if (ret != GG_ERR_OK) {
        return 1;
    }
}
