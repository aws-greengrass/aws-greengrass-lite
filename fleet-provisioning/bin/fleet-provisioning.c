// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include <argp.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/version.h>
#include <string.h>

__attribute__((visibility("default"))) const char *argp_program_version
    = GGL_VERSION;

static char doc[] = "fleet provisioner -- Executable to automatically "
                    "provision the device to AWS IOT core";
static const char COMPONENT_NAME[] = "fleet-provisioning";

static struct argp_option opts[] = {
    { "claim-key",
      'k',
      "path",
      0,
      "Path to key for client claim private certificate",
      0 },
    { "claim-cert",
      'c',
      "path",
      0,
      "Path to key for client claim certificate",
      0 },
    { "template-name",
      't',
      "name",
      0,
      "AWS fleet provisioning template name",
      0 },
    { "template-param",
      'p',
      "json",
      0,
      "[optional] Fleet Prov additional parameters",
      0 },
    { "data-endpoint", 'e', "name", 0, "AWS IoT Core data endpoint", 0 },
    { "root-ca-path", 'r', "path", 0, "Path to key for client certificate", 0 },
    { 0 }
};

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
    case ARGP_KEY_END:
        // ALL keys have defaults further in.
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

static void parse_path(char **argv, char *path) {
    // The passed in buffer is expected to be initialzed already, No need to
    // worry about null termination
    //  NOLINTNEXTLINE(bugprone-not-null-terminated-result)
    memcpy(path, argv[0], strlen(argv[0]) - strlen(COMPONENT_NAME));
    strncat(path, "iotcored", strlen("iotcored"));

    GGL_LOGD("iotcored path: %.*s", (int) strlen(path), path);
}

int main(int argc, char **argv) {
    static FleetProvArgs args = { 0 };
    static char iotcored_path[4097] = { 0 };

    parse_path(argv, iotcored_path);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);
    args.iotcored_path = iotcored_path;

    GglError ret = run_fleet_prov(&args);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
