// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ggl/rand.h"
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <stdlib.h>

static int random_fd;

__attribute__((constructor)) static void init_urandom_fd(void) {
    random_fd = open("/dev/random", O_RDONLY | O_CLOEXEC);
    if (random_fd == -1) {
        int err = errno;
        GGL_LOGE("Failed to open /dev/random: %d.", err);
        _Exit(1);
    }
}

GglError ggl_rand_fill(GglBuffer buf) {
    GglError ret = ggl_file_read_exact(random_fd, buf);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read from /dev/random.");
    }
    return ret;
}
