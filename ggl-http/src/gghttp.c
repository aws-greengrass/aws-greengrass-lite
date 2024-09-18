// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttp_util.h"
#include "ggl/error.h"
#include "ggl/http.h"
#include "ggl/object.h"
#include <ggl/log.h>
#include <stdio.h>

GglError fetch_token(
    const char *url_for_token,
    GglBuffer thing_name,
    CertificateDetails certificate_details,
    GglBuffer *buffer
) {
    CurlData curl_data = { 0 };

    GGL_LOGI(
        "fetch_token",
        "Fetching token from credentials endpoint=%s, for iot thing=%.*s",
        url_for_token,
        (int) thing_name.len,
        thing_name.data
    );

    GglError error = gghttplib_init_curl(&curl_data, url_for_token);
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data, GGL_STR("x-amzn-iot-thingname"), thing_name
        );
    }
    if (error == GGL_ERR_OK) {
        gghttplib_add_certificate_data(&curl_data, certificate_details);
        error = gghttplib_process_request(&curl_data, buffer);
    }

    return error;
}

GglError generic_download(
    const char *url_for_generic_download, const char *file_path
) {
    GGL_LOGI(
        "generic_download",
        "downloading content from %s and storing to %s",
        url_for_generic_download,
        file_path
    );

    FILE *file_pointer = fopen(file_path, "wb");
    if (file_pointer == NULL) {
        return GGL_ERR_FAILURE;
    }

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_generic_download);
    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request_with_file_pointer(
            &curl_data, file_pointer
        );
    }

    fclose(file_pointer);

    return error;
}

GglError sigv4_download(
    const char *url_for_sigv4_download,
    const char *file_path,
    SigV4Details sigv4_details
) {
    GGL_LOGI(
        "sigv4_download",
        "downloading content from %s and storing to %s",
        url_for_sigv4_download,
        file_path
    );
    FILE *file_pointer = fopen(file_path, "wb");
    if (file_pointer == NULL) {
        return GGL_ERR_FAILURE;
    }

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_sigv4_download);
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_sigv4_credential(&curl_data, sigv4_details);
    }
    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request_with_file_pointer(
            &curl_data, file_pointer
        );
    }

    fclose(file_pointer);

    return error;
}
