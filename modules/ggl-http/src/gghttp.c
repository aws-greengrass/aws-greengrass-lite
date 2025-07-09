// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws_sigv4.h"
#include "gghttp_util.h"
#include "ggl/error.h"
#include "ggl/http.h"
#include <assert.h>
#include <curl/curl.h>
#include <ggl/buffer.h>
#include <ggl/log.h>
#include <ggl/vector.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_URI_LENGTH 4096
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
        error = gghttplib_add_certificate_data(&curl_data, certificate_details);
    }

    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request(&curl_data, buffer);
    }

    long http_status_code = 0;
    curl_easy_getinfo(curl_data.curl, CURLINFO_HTTP_CODE, &http_status_code);
    GGL_LOGI("HTTP code: %ld", http_status_code);

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

    long http_status_code = 0;
    curl_easy_getinfo(curl_data.curl, CURLINFO_HTTP_CODE, &http_status_code);
    GGL_LOGD("Return HTTP code: %ld", http_status_code);

    gghttplib_destroy_curl(&curl_data);
    return error;
}

GglError sigv4_download(
    const char *url_for_sigv4_download,
    GglBuffer host,
    GglBuffer file_path,
    int fd,
    SigV4Details sigv4_details,
    uint16_t *http_response_code
) {
    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, url_for_sigv4_download);
    uint8_t arr[2048];
    GglByteVec vec = GGL_BYTE_VEC(arr);
    uint8_t time_buffer[17];
    size_t date_len
        = aws_sigv4_get_iso8601_time((char *) time_buffer, sizeof(time_buffer));
    uint8_t auth_buf[256];
    GglBuffer auth_header = GGL_BUF(auth_buf);

    assert(date_len > 0);

    S3RequiredHeaders required_headers
        = { .amz_content_sha256 = GGL_STR(ZERO_PAYLOAD_SHA),
            .amz_date = (GglBuffer) { .data = time_buffer, .len = date_len },
            .amz_security_token = sigv4_details.session_token,
            .host = host };

    // Add the content sha header to the curl headers too.
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GGL_STR("x-amz-content-sha256"),
            // Signature of empty payload is constant.
            GGL_STR(ZERO_PAYLOAD_SHA)
        );
    }

    // Add the amz-date header to the curl headers too.
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GGL_STR("x-amz-date"),
            (GglBuffer) { .data = time_buffer, .len = date_len }
        );
    }

    // Add the amz-security-token header to the curl headers too.
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GGL_STR("x-amz-security-token"),
            sigv4_details.session_token
        );
    }

    if (error == GGL_ERR_NOMEM) {
        GGL_LOGE("The array 'arr' is not big enough to accommodate the headers."
        );
    }

    // We DO NOT need to add the "host" header to curl as that is added
    // automatically by curl.

    if (error == GGL_ERR_OK) {
        error = aws_sigv4_s3_get_create_header(
            file_path, sigv4_details, required_headers, &vec, &auth_header
        );
    }

    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data, GGL_STR("Authorization"), auth_header
        );
    }

    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request_with_fd(&curl_data, fd);
    }

    long http_status_code = 0;
    curl_easy_getinfo(curl_data.curl, CURLINFO_HTTP_CODE, &http_status_code);
    GGL_LOGD("Return HTTP code: %ld", http_status_code);

    if (http_status_code >= 0) {
        *http_response_code = (uint16_t) http_status_code;
    } else {
        *http_response_code = 400;
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
        ret = gghttplib_add_certificate_data(&curl_data, certificate_details);
    }
    if (ret == GGL_ERR_OK) {
        if (body != NULL) {
            GGL_LOGD("Adding body to http request");
            ret = gghttplib_add_post_body(&curl_data, body);
        }
    }
    if (ret == GGL_ERR_OK) {
        GGL_LOGD("Sending request to dataplane endpoint");
        ret = gghttplib_process_request(&curl_data, response_buffer);
    }

    long http_status_code = 0;
    curl_easy_getinfo(curl_data.curl, CURLINFO_HTTP_CODE, &http_status_code);
    GGL_LOGI("HTTP code: %ld", http_status_code);

    gghttplib_destroy_curl(&curl_data);

    return ret;
}
