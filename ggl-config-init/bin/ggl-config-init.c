// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/yaml_decode.h>
#include <stddef.h>
#include <stdint.h>

static char *config_path = NULL;

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

    if (config_path == NULL) {
        GGL_LOGE("Invalid config path.");
        return 1;
    }

    static uint8_t file_mem[8192];
    GglBuffer config_file = GGL_BUF(file_mem);

    GglError ret = ggl_file_read_path(
        ggl_buffer_from_null_term(config_path), &config_file
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to read config file.");
        return 1;
    }

    static uint8_t decode_mem[500 * sizeof(GglObject)];
    GglBumpAlloc balloc = ggl_bump_alloc_init(GGL_BUF(decode_mem));

    GglObject config_obj;
    ret = ggl_yaml_decode_destructive(config_file, &balloc.alloc, &config_obj);
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse config file.");
        return 1;
    }

    GGL_LOGI("Updating gg_config configuration.");
    ret = ggl_gg_config_write(GGL_BUF_LIST(), config_obj, &(int64_t) { 0 });
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to update configuration: %d.", ret);
        return 1;
    }
}
