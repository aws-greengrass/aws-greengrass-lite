/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggconfig.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include <stdlib.h>

static void exit_cleanup(void) {
    ggconfig_close();
}

int main(int argc, char **argv) {
    (void) argc;
    (void) argv;

    atexit(exit_cleanup);

    ggconfig_open();

    ggl_listen(GGL_STR("/aws/ggl/ggconfigd"), NULL);

    return 0;
}
