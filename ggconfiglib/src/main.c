/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ggconfig.h"
#include "ggl/error.h"
#include "ggl/object.h"
#include "ggl/server.h"
#include <argp.h>
#include <stdlib.h>

const char *program_version = "ggconfigd 0.0.1";
const char *bug_address = "ggteam@amazon.com";
static char doc[] = "ggconfigd -- configuration management";

static struct argp argp = { 0, 0, 0, doc };

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, 0);

    ggconfig_open();

    ggl_listen(GGL_STR("/aws/ggl/ggconfigd"), NULL);

    ggconfig_close();

    return 0;
}
