#include "aws_sigv4.h"
#include "gghttp_util.h"
#include <assert.h>
#include <curl/curl.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/api_ecr.h>
#include <ggl/http.h>
#include <stddef.h>
#include <stdint.h>

GgError ggl_http_ecr_get_authorization_token(
    SigV4Details sigv4_details,
    uint16_t *http_response_code,
    GgBuffer *response_buffer
) {
    uint8_t url_buf[64] = { 0 };
    GgByteVec url_vec = GG_BYTE_VEC(url_buf);
    GgError err = GG_ERR_OK;
    gg_byte_vec_chain_append(&err, &url_vec, GG_STR("https://"));
    gg_byte_vec_chain_append(&err, &url_vec, GG_STR("api.ecr."));
    gg_byte_vec_chain_append(&err, &url_vec, sigv4_details.aws_region);
    gg_byte_vec_chain_append(&err, &url_vec, GG_STR(".amazonaws.com\0"));
    if (err != GG_ERR_OK) {
        return GG_ERR_NOMEM;
    }

    uint8_t host_buf[64];
    GgByteVec host_vec = GG_BYTE_VEC(host_buf);
    gg_byte_vec_chain_append(&err, &host_vec, GG_STR("api.ecr."));
    gg_byte_vec_chain_append(&err, &host_vec, sigv4_details.aws_region);
    gg_byte_vec_chain_append(&err, &host_vec, GG_STR(".amazonaws.com"));
    if (err != GG_ERR_OK) {
        return GG_ERR_NOMEM;
    }

    CurlData curl_data = { 0 };
    GgError error = gghttplib_init_curl(&curl_data, (const char *) url_buf);
    uint8_t headers_array[512];
    GgByteVec vec = GG_BYTE_VEC(headers_array);
    uint8_t time_buffer[17];
    size_t date_len
        = aws_sigv4_get_iso8601_time((char *) time_buffer, sizeof(time_buffer));
    uint8_t auth_buf[512];
    GgBuffer auth_header = GG_BUF(auth_buf);

    assert(date_len > 0);

    ECRRequiredHeaders required_headers
        = { .content_type = GG_STR("application/x-amz-json-1.1"),
            .host = host_vec.buf,
            .amz_date = (GgBuffer) { .data = time_buffer, .len = date_len },
            .payload = GG_STR("{}") };

    if (error == GG_ERR_OK) {
        error = aws_sigv4_ecr_post_create_header(
            GG_STR("/"), sigv4_details, required_headers, &vec, &auth_header
        );
    }

    if (error == GG_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data, GG_STR("Authorization"), auth_header
        );
    }

    // Add the amz-date header to the curl headers too.
    if (error == GG_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GG_STR("x-amz-date"),
            (GgBuffer) { .data = time_buffer, .len = 16 }
        );
    }

    // Token needed to AuthN/AuthZ the action
    if (error == GG_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GG_STR("x-amz-security-token"),
            sigv4_details.session_token
        );
    }

    // Add amz-target header so ECR knows which Action and Version we are using
    if (error == GG_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GG_STR("x-amz-target"),
            GG_STR("AmazonEC2ContainerRegistry_V20150921.GetAuthorizationToken")
        );
    }

    // ECR needs to know the POST body is JSON
    if (error == GG_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GG_STR("Content-Type"),
            GG_STR("application/x-amz-json-1.1")
        );
    }

    if (error == GG_ERR_OK) {
        error = gghttplib_add_post_body(&curl_data, "{}");
    }

    if (error == GG_ERR_NOMEM) {
        GG_LOGE("The array 'arr' is not big enough to accommodate the headers."
        );
    }

    // We DO NOT need to add the "host" header to curl as that is added
    // automatically by curl.

    if (error == GG_ERR_OK) {
        error = gghttplib_process_request(&curl_data, response_buffer);
    }

    long http_status_code = 0;
    curl_easy_getinfo(curl_data.curl, CURLINFO_HTTP_CODE, &http_status_code);
    GG_LOGD("Return HTTP code: %ld", http_status_code);

    if (http_status_code >= 0) {
        *http_response_code = (uint16_t) http_status_code;
    } else {
        *http_response_code = 400;
    }

    gghttplib_destroy_curl(&curl_data);

    return error;
}
