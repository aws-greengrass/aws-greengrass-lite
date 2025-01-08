// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include <sys/types.h>
#include <argp.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/exec.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/vector.h>
#include <ggl/version.h>
#include <string.h>
#include <stdint.h>

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

static char doc[] = "fleet provisioner -- Executable to automatically "
                    "provision the device to AWS IOT core";
static GglBuffer component_name = GGL_STR("fleet-provisioning");

static struct argp_option opts[]
    = { { "user-group",
          'u',
          "name",
          0,
          "[optional]GGL_SYSTEMD_SYSTEM_USER user and group \":\" seprated",
          0 },
        { "claim-key",
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
    case 'u':
        args->user_group = arg;
        break;
    case ARGP_KEY_END:
        if (args->user_group == NULL) {
            args->user_group = "ggcore:ggcore";
        }
        // All keys are optional other are set down the line
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

// Use the execution path in argv[0] to find iotcored
static void parse_path(char **argv, GglBuffer *path) {
    GglBuffer execution_name = ggl_buffer_from_null_term(argv[0]);
    GglByteVec path_to_iotcored = ggl_byte_vec_init(*path);
    if (ggl_buffer_has_suffix(execution_name, component_name)) {
        ggl_byte_vec_append(
            &path_to_iotcored,
            ggl_buffer_substr(
                execution_name, 0, execution_name.len - component_name.len
            )
        );
    }
    ggl_byte_vec_append(&path_to_iotcored, GGL_STR("iotcored\0"));
    path->len = path_to_iotcored.buf.len - 1;

    GGL_LOGD("iotcored path: %.*s", (int) path->len, path->data);
}

int main(int argc, char **argv) {
    static FleetProvArgs args = { 0 };
    static uint8_t iotcored_path[4097] = { 0 };

    parse_path(argv, &GGL_BUF(iotcored_path));

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);
    args.iotcored_path = (char *) iotcored_path;

    pid_t pid = -1;
    GglError ret = run_fleet_prov(&args, &pid);
    if (ret != GGL_ERR_OK) {
        if (pid != -1) {
            GGL_LOGE("Something went wrong. Killing iotcored");
            ggl_exec_kill_process(pid);
        }
        return 1;
    }
}
