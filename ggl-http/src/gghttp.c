// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttp_utll.h"
#include "ggl/error.h"
#include "ggl/http.h"
#include "ggl/object.h"
#include <ggl/log.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static const char HEADER_KEY[] = "x-amzn-iot-thingname";

void fetch_token(
    const char *url_for_token,
    const char *thing_name,
    CertificateDetails certificate_details,
    GglBuffer *buffer
) {
    CurlData curl_data = { 0 };

    GGL_LOGI(
        "fetch_token",
        "Fetching token from credentials endpoint=%s, for iot thing=%s",
        url_for_token,
        thing_name
    );

    GglError error = gghttplib_init_curl(&curl_data, url_for_token);
    if (error == GGL_ERR_OK) {
        gghttplib_add_header(&curl_data, HEADER_KEY, thing_name);
        gghttplib_add_certificate_data(&curl_data, certificate_details);
        gghttplib_process_request(&curl_data, buffer);
    }
}

void generic_download(
    const char *url_for_generic_download, const char *file_path
) {
    GGL_LOGI(
        "generic_download",
        "downloading content from %s and storing to %s",
        url_for_generic_download,
        file_path
    );

    CurlData curl_data = { 0 };
    FILE *file_pointer;

    GglError error = gghttplib_init_curl(&curl_data, url_for_generic_download);
    if (error == GGL_ERR_OK) {
        file_pointer = fopen(file_path, "wb");

        GglError ret = gghttplib_process_request_with_file_pointer(
            &curl_data, file_pointer
        );

        if (ret != GGL_ERR_OK) {
            return;
        }
    }
}
