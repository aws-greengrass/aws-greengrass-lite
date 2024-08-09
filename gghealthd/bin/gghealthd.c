/* ggl - Utilities for AWS IoT Core clients
 * Copyright (C) 2024 Amazon.com, Inc. or its affiliates
 */

#include "bus_server.h"
#include <ggl/error.h>

int main(void) {
    GglError ret = run_gghealthd();
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
