// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include <argp.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/log.h>
#include <ggl/nucleus/init.h>
#include <ggl/vector.h>
#include <sys/types.h>
#include <stdint.h>

static char doc[] = "fleet provisioner -- Executable to automatically "
                    "provision the device to AWS IOT core";
static GglBuffer component_name = GGL_STR("fleet-provisioning");

static struct argp_option opts[]
    = { { "claim-key",
          'k',
          "path",
          0,
          "[optional]Path to key for client claim private certificate",
          0 },
        { "claim-cert",
          'c',
          "path",
          0,
          "[optional]Path to key for client claim certificate",
          0 },
        { "template-name",
          't',
          "name",
          0,
          "[optional]AWS fleet provisioning template name",
          0 },
        { "template-param",
          'p',
          "json",
          0,
          "[optional]Fleet Prov additional parameters",
          0 },
        { "data-endpoint",
          'e',
          "name",
          0,
          "[optional]AWS IoT Core data endpoint",
          0 },
        { "root-ca-path",
          'r',
          "path",
          0,
          "[optional]Path to key for client certificate",
          0 },
        { "out-cert-path",
          'o',
          "path",
          0,
          "[optional]Path to the location of generated certificates",
          0 },
        { 0 } };

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    FleetProvArgs *args = state->input;
    switch (key) {
    case 'c':
        args->claim_cert_path = arg;
        break;
    case 'k':
        args->claim_key_path = arg;
        break;
    case 't':
        args->template_name = arg;
        break;
    case 'p':
        args->template_parameters = arg;
        break;
    case 'e':
        args->data_endpoint = arg;
        break;
    case 'r':
        args->root_ca_path = arg;
        break;
    case 'o':
        args->out_cert_path = arg;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

// Use the execution path in argv[0] to find iotcored
static GglError parse_path(char **argv, GglBuffer *path) {
    GglBuffer execution_name = ggl_buffer_from_null_term(argv[0]);
    GglByteVec path_to_iotcored = ggl_byte_vec_init(*path);
    if (ggl_buffer_has_suffix(execution_name, component_name)) {
        GglError ret = ggl_byte_vec_append(
            &path_to_iotcored,
            ggl_buffer_substr(
                execution_name, 0, execution_name.len - component_name.len
            )
        );
        if (ret != GGL_ERR_OK) {
            return ret;
        }
    }
    GglError ret
        = ggl_byte_vec_append(&path_to_iotcored, GGL_STR("iotcored\0"));
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    path->len = path_to_iotcored.buf.len - 1;

    GGL_LOGD("iotcored path: %.*s", (int) path->len, path->data);
    return GGL_ERR_OK;
}

int main(int argc, char **argv) {
    static FleetProvArgs args = { 0 };
    static uint8_t iotcored_path[4097] = { 0 };

    GglError ret = parse_path(argv, &GGL_BUF(iotcored_path));
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to initialize iotcored path.");
        return 1;
    }

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);
    args.iotcored_path = (char *) iotcored_path;

    ggl_nucleus_init();

    pid_t pid = -1;
    ret = run_fleet_prov(&args, &pid);
    if (ret != GGL_ERR_OK) {
        if (pid != -1) {
            GGL_LOGE("Something went wrong. Killing iotcored");
            (void) ggl_exec_kill_process(pid);
        }
        return 1;
    }
}
