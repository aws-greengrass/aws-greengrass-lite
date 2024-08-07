// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttplibtest.h"
#include <ggl/error.h>
#include <ggl/gghttplib.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <stdio.h>
#include <stdlib.h>

GglError test_gghttplib(GgHttpLibArgs *args) {
    GglError ret = GGL_ERR_OK;

    GGL_LOGI(
        "test_gghttplib",
        "Data recieeved is : \n %s %s %s %s %s %s",
        args->cert,
        args->file_path,
        args->key,
        args->rootca,
        args->thing_name,
        args->url
    );

    if (args->cert == NULL && args->rootca == NULL && args->key == NULL) {
        GGL_LOGI("test_gghttplib", "Generic file download operation");
        generic_download(args->url, args->file_path);
        GGL_LOGI(
            "test_gghttplib",
            "Please check the file for download content: \n %s",
            args->file_path
        );
    } else {
        CertificateDetails certificate = { 0 };
        certificate.gghttplib_cert_path = args->cert;
        certificate.gghttplib_root_ca_path = args->rootca;
        certificate.gghttplib_p_key_path = args->key;

        GglBuffer buffer
            = fetch_token(args->url, args->thing_name, certificate);
        GGL_LOGI(
            "test_gghttplib", "The credentials recieved are: \n %s", buffer.data
        );
        free(buffer.data);
    }
    return ret;
}
