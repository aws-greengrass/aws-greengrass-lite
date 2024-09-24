// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <assert.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/yaml_decode.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <stddef.h>
#include <stdint.h>

char *config_path = NULL;

static char doc[] = "ggl-config-init -- Update Greengrass Lite configuration";

static struct argp_option opts[] = {
    { "config", 'c', "path", 0, "Path to configuration file", 0 },
    { 0 },
};

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    (void) arg;
    switch (key) {
    case 'c':
        config_path = arg;
        break;
    case ARGP_KEY_END:
        if (config_path == NULL) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            argp_usage(state);
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

int main(int argc, char **argv) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    assert(config_path != NULL);

    int fd = open(config_path, O_RDONLY);
    if (fd == -1) {
        GGL_LOGE("main", "Failed to open config file.");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        GGL_LOGE("main", "Failed to get config file info.");
        return 1;
    }

    size_t file_size = (size_t) st.st_size;
    uint8_t *file_str
        = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    if (file_str == MAP_FAILED) {
        GGL_LOGE("main", "Failed to load config file.");
        return 1;
    }

    GglBuffer config_file = { .data = file_str, .len = file_size };

    static uint8_t decode_mem[500 * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(decode_mem));

    GglObject config_obj;
    GglError ret
        = ggl_yaml_decode_destructive(config_file, &balloc.alloc, &config_obj);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("main", "Failed to parse config file.");
        return 1;
    }

    ret = ggl_gg_config_write(GGL_BUF_LIST(), config_obj, 0);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("client", "Failed to update configuration: %d.", ret);
        return 1;
    }
}
