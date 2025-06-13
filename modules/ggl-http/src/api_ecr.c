#include "ggl/api_ecr.h"
#include "aws_sigv4.h"
#include "gghttp_util.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/vector.h>
#include <stdint.h>

// hash of {}
#define EMPTY_JSON_SHA256 \
    "44136fa355b3678a1146ad16f7e8649e94fb4fc21fe77e8310c060f61caaff8a"

GglError ggl_http_ecr_get_authorization_token(
    SigV4Details sigv4_details,
    uint16_t *http_response_code,
    GglBuffer *response_buffer
) {
    uint8_t url_buf[64] = { 0 };
    GglByteVec url_vec = GGL_BYTE_VEC(url_buf);
    GglError err = GGL_ERR_OK;
    ggl_byte_vec_chain_append(&err, &url_vec, GGL_STR("https://"));
    ggl_byte_vec_chain_append(&err, &url_vec, GGL_STR("ecr."));
    ggl_byte_vec_chain_append(&err, &url_vec, sigv4_details.aws_region);
    ggl_byte_vec_chain_append(&err, &url_vec, GGL_STR(".api.aws\0"));
    if (err != GGL_ERR_OK) {
        return GGL_ERR_NOMEM;
    }

    uint8_t host_buf[64];
    GglByteVec host_vec = GGL_BYTE_VEC(host_buf);
    ggl_byte_vec_chain_append(&err, &host_vec, GGL_STR("ecr."));
    ggl_byte_vec_chain_append(&err, &host_vec, sigv4_details.aws_region);
    ggl_byte_vec_chain_append(&err, &host_vec, GGL_STR(".api.aws\0"));
    if (err != GGL_ERR_OK) {
        return GGL_ERR_NOMEM;
    }

    CurlData curl_data = { 0 };
    GglError error = gghttplib_init_curl(&curl_data, (const char *) url_buf);
    uint8_t arr[2048];
    GglByteVec vec = GGL_BYTE_VEC(arr);
    uint8_t time_buffer[17];
    size_t date_len
        = aws_sigv4_get_iso8601_time((char *) time_buffer, sizeof(time_buffer));
    uint8_t auth_buf[256];
    GglBuffer auth_header = GGL_BUF(auth_buf);

    assert(date_len > 0);

    if (error == GGL_ERR_OK) {
        error = gghttplib_add_post_body(&curl_data, "{}");
    }

    ECRRequiredHeaders required_headers = {
        .amz_target
        = GGL_STR("AmazonEC2ContainerRegistry_V20150921.GetAuthorizationToken"),
        .amz_date = (GglBuffer) { .data = time_buffer, .len = date_len },
        .amz_security_token = sigv4_details.session_token,
        .host = host_vec.buf
    };

    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GGL_STR("Content-Type"),
            GGL_STR("application/x-amz-json-1.1")
        );
    }

    // Add amz-target header so ECR knows which Operation we are using
    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data,
            GGL_STR("x-amz-target"),
            GGL_STR("AmazonEC2ContainerRegistry_V20150921.GetAuthorizationToken"
            )
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
        error = aws_sigv4_ecr_post_create_header(
            GGL_STR("/"), sigv4_details, required_headers, &vec, &auth_header
        );
    }

    if (error == GGL_ERR_OK) {
        error = gghttplib_add_header(
            &curl_data, GGL_STR("Authorization"), auth_header
        );
    }

    if (error == GGL_ERR_OK) {
        error = gghttplib_process_request(&curl_data, response_buffer);
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