// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttp_util.h"
#include "ggl/error.h"
#include "ggl/http.h"
#include "ggl/object.h"
#include <ggl/buffer.h>
#include <ggl/log.h>
#include <ggl/vector.h>
#include <stdio.h>

#define MAX_URI_LENGTH 2048
#define HTTPS_PREFIX "https://"

GglError fetch_token(
    const char *url_for_token,
    GglBuffer thing_name,
    CertificateDetails certificate_details,
    GglBuffer *buffer
) {
    CurlData curl_data = { 0 };

    GGL_LOGI(
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

    gghttplib_destroy_curl(&curl_data);

    return error;
}

GglError generic_download(const char *url_for_generic_download, int fd) {
    GGL_LOGI("downloading content from %s", url_for_generic_download);

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_generic_download);
    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request_with_fd(&curl_data, fd);
    }

    return error;
}

GglError sigv4_download(
    const char *url_for_sigv4_download, int fd, SigV4Details sigv4_details
) {
    GGL_LOGI("downloading content from %s", url_for_sigv4_download);

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_sigv4_download);
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_sigv4_credential(&curl_data, sigv4_details);
    }
    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request_with_fd(&curl_data, fd);
    }

    gghttplib_destroy_curl(&curl_data);

    return error;
}

GglError gg_dataplane_call(
    GglBuffer endpoint,
    GglBuffer port,
    GglBuffer uri_path,
    CertificateDetails certificate_details,
    const char *body,
    GglBuffer *response_buffer
) {
    CurlData curl_data = { 0 };

    GGL_LOGI(
        "Preparing call to data endpoint provided as %.*s:%.*s/%.*s",
        (int) endpoint.len,
        endpoint.data,
        (int) port.len,
        port.data,
        (int) uri_path.len,
        uri_path.data
    );

    static char uri_buf[MAX_URI_LENGTH];
    GglByteVec uri_vec = GGL_BYTE_VEC(uri_buf);
    GglError ret = ggl_byte_vec_append(&uri_vec, GGL_STR(HTTPS_PREFIX));
    ggl_byte_vec_chain_append(&ret, &uri_vec, endpoint);
    ggl_byte_vec_chain_push(&ret, &uri_vec, ':');
    ggl_byte_vec_chain_append(&ret, &uri_vec, port);
    ggl_byte_vec_chain_push(&ret, &uri_vec, '/');
    ggl_byte_vec_chain_append(&ret, &uri_vec, uri_path);
    ggl_byte_vec_chain_push(&ret, &uri_vec, '\0');
    if (ret != GGL_ERR_OK) {
        return GGL_ERR_NOMEM;
    }

    ret = gghttplib_init_curl(&curl_data, uri_buf);

    if (ret == GGL_ERR_OK) {
        ret = gghttplib_add_header(
            &curl_data, GGL_STR("Content-type"), GGL_STR("application/json")
        );
    }
    if (ret == GGL_ERR_OK) {
        gghttplib_add_certificate_data(&curl_data, certificate_details);
        if (body != NULL) {
            GGL_LOGD("Adding body to http request");
            gghttplib_add_post_body(&curl_data, body);
        }
        GGL_LOGD("Sending request to dataplane endpoint");
        ret = gghttplib_process_request(&curl_data, response_buffer);
    }

    gghttplib_destroy_curl(&curl_data);

    return ret;
}
