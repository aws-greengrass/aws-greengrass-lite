// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "fleet-provisioning.h"
#include <argp.h>
#include <ggl/binpath.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/nucleus/init.h>
#include <ggl/vector.h>

static char doc[] = "fleet-provisioning -- AWS IoT Fleet Provisioning client";

static struct argp_option opts[]
    = { { "claim-key",
          'k',
          "path",
          OPTION_ARG_OPTIONAL,
          "Client claim certificate private key path",
          0 },
        { "claim-cert",
          'c',
          "path",
          OPTION_ARG_OPTIONAL,
          "Client claim certificate path",
          0 },
        { "template-name",
          't',
          "name",
          OPTION_ARG_OPTIONAL,
          "AWS IoT Fleet Provisioning template name",
          0 },
        { "template-params",
          'p',
          "json",
          OPTION_ARG_OPTIONAL,
          "AWS IoT Fleet Provisioning additional parameters",
          0 },
        { "endpoint",
          'e',
          "domain_name",
          OPTION_ARG_OPTIONAL,
          "AWS IoT Core data endpoint",
          0 },
        { "root-ca-path",
          'r',
          "path",
          OPTION_ARG_OPTIONAL,
          "Path to the Root CA certificate",
          0 },
        { "output-dir",
          'o',
          "path",
          OPTION_ARG_OPTIONAL,
          "Directory for storing generated files",
          0 },
        { 0 } };

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    FleetProvArgs *args = state->input;
    switch (key) {
    case 'c':
        args->claim_cert = arg;
        break;
    case 'k':
        args->claim_key = arg;
        break;
    case 't':
        args->template_name = arg;
        break;
    case 'p':
        args->template_params = arg;
        break;
    case 'r':
        args->root_ca_path = arg;
        break;
    case 'e':
        args->endpoint = arg;
        break;
    case 'o':
        args->output_dir = arg;
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

int main(int argc, char **argv) {
    static FleetProvArgs args = { 0 };
    static char iotcored_path_buf[256];

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    ggl_nucleus_init();

    // Parse binary path and set iotcored_path
    GglByteVec iotcored_path = GGL_BYTE_VEC(iotcored_path_buf);

    GglError ret = ggl_binpath_append_name(
        ggl_buffer_from_null_term(argv[0]), GGL_STR("iotcored"), &iotcored_path
    );
    if (ret == GGL_ERR_OK) {
        args.iotcored_path = (char *) iotcored_path.buf.data;
    }

    ret = run_fleet_prov(&args);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
}
