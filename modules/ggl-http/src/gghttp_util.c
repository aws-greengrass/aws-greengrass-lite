// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttp_util.h"
#include "ggl/error.h"
#include "ggl/http.h"
#include "ggl/object.h"
#include <sys/types.h>
#include <assert.h>
#include <curl/curl.h>
#include <ggl/cleanup.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/vector.h>
#include <pthread.h>
#include <stdlib.h>

#define MAX_HEADER_LENGTH 1024

__attribute__((constructor)) static void init_curl(void) {
    // TODO: set up a heap4 and init curl instead with curl_global_init_mem()
    CURLcode e = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (e != CURLE_OK) {
        GGL_LOGE(
            "Failed to init curl with CURLcode %d (reason: \"%s\").",
            e,
            curl_easy_strerror(e)
        );
        _Exit(1);
    }
}

static GglError translate_curl_code(CURLcode code) {
    switch (code) {
    case CURLE_OK:
        return GGL_ERR_OK;
    case CURLE_AGAIN:
        return GGL_ERR_RETRY;
    case CURLE_URL_MALFORMAT:
        return GGL_ERR_PARSE;
    case CURLE_ABORTED_BY_CALLBACK:
    case CURLE_WRITE_ERROR:
        return GGL_ERR_FAILURE;
    default:
        return GGL_ERR_REMOTE;
    }
}

/// @brief Callback function to write the HTTP response data to a buffer.
///
/// This function is used as a callback by CURL to handle the response data
/// received from an HTTP request. It reallocates memory for the output buffer
/// and copies the response data into the buffer.This function will be called
/// multiple times when a new data is fetched via libcurl.
///
/// @param[in] response_data A pointer to the response data received from CURL.
/// @param[in] size The size of each element in the response data.
/// @param[in] nmemb The number of elements in the response data.
/// @param[in] output_vector_void A pointer to a vector which will be appended.
///
/// @return The number of bytes written to the output buffer.
static size_t write_response_to_buffer(
    void *response_data, size_t size, size_t nmemb, void *output_vector_void
) {
    if (response_data == NULL) {
        return 0;
    }
    size_t size_of_response_data = size * nmemb;
    GglBuffer response_buffer
        = (GglBuffer) { .data = response_data, .len = size_of_response_data };
    assert(output_vector_void != NULL);
    GglByteVec *output_vector = output_vector_void;
    GglError ret = ggl_byte_vec_append(output_vector, response_buffer);
    if (ret != GGL_ERR_OK) {
        size_t remaining_capacity
            = ggl_byte_vec_remaining_capacity(*output_vector).len;
        GGL_LOGE(
            "Not enough space to hold full body. Est. remaining bytes: %zu. "
            "Buffer remaining capacity: %zu",
            size_of_response_data,
            remaining_capacity
        );
        return 0;
    }

    return size_of_response_data;
}

/// @brief Callback function to write the HTTP response data to a file
/// descriptor.
///
/// This function is used as a callback by CURL to handle the response data
/// received from an HTTP request. It write bytes received into the file
/// descriptor.
///
/// @param[in] response_data A pointer to the response data received from CURL.
/// @param[in] size The size of each element in the response data.
/// @param[in] nmemb The number of elements in the response data.
/// @param[in] fd_void A pointer to a file descriptor
///
/// @return The number of bytes written.
static size_t write_response_to_fd(
    void *response_data, size_t size, size_t nmemb, void *fd_void
) {
    if (response_data == NULL) {
        return 0;
    }
    size_t size_of_response_data = size * nmemb;
    GglBuffer response_buffer
        = (GglBuffer) { .data = response_data, .len = size_of_response_data };
    assert(fd_void != NULL);
    int *fd = (int *) fd_void;
    GglError err = ggl_file_write(*fd, response_buffer);
    if (err != GGL_ERR_OK) {
        return 0;
    }
    return size_of_response_data;
}

void gghttplib_destroy_curl(CurlData *curl_data) {
    assert(curl_data != NULL);
    if (curl_data->headers_list != NULL) {
        curl_slist_free_all(curl_data->headers_list);
        curl_data->headers_list = NULL;
    }
    curl_easy_cleanup(curl_data->curl);
}

GglError gghttplib_init_curl(CurlData *curl_data, const char *url) {
    curl_data->headers_list = NULL;
    curl_data->curl = curl_easy_init();

    if (curl_data->curl == NULL) {
        GGL_LOGE("Cannot create instance of curl for the url=%s", url);
        return GGL_ERR_FAILURE;
    }

    CURLcode err = curl_easy_setopt(curl_data->curl, CURLOPT_URL, url);
    return translate_curl_code(err);
}

GglError gghttplib_add_header(
    CurlData *curl_data, GglBuffer header_key, GglBuffer header_value
) {
    assert(curl_data != NULL);
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GGL_MTX_SCOPE_GUARD(&mtx);
    static char header[MAX_HEADER_LENGTH];
    GglByteVec header_vec = GGL_BYTE_VEC(header);
    GglError err = GGL_ERR_OK;
    // x-header-key: header-value
    ggl_byte_vec_chain_append(&err, &header_vec, header_key);
    ggl_byte_vec_chain_push(&err, &header_vec, ':');
    ggl_byte_vec_chain_push(&err, &header_vec, ' ');
    ggl_byte_vec_chain_append(&err, &header_vec, header_value);
    ggl_byte_vec_chain_push(&err, &header_vec, '\0');
    if (err != GGL_ERR_OK) {
        return err;
    }
    struct curl_slist *new_head
        = curl_slist_append(curl_data->headers_list, header);
    if (new_head == NULL) {
        return GGL_ERR_FAILURE;
    }
    curl_data->headers_list = new_head;
    return GGL_ERR_OK;
}

GglError gghttplib_add_certificate_data(
    CurlData *curl_data, CertificateDetails request_data
) {
    assert(curl_data != NULL);
    CURLcode err = curl_easy_setopt(
        curl_data->curl, CURLOPT_SSLCERT, request_data.gghttplib_cert_path
    );
    if (err != CURLE_OK) {
        return translate_curl_code(err);
    }
    err = curl_easy_setopt(
        curl_data->curl, CURLOPT_SSLKEY, request_data.gghttplib_p_key_path
    );
    if (err != CURLE_OK) {
        return translate_curl_code(err);
    }
    err = curl_easy_setopt(
        curl_data->curl, CURLOPT_CAINFO, request_data.gghttplib_root_ca_path
    );
    return translate_curl_code(err);
}

GglError gghttplib_add_post_body(CurlData *curl_data, const char *body) {
    assert(curl_data != NULL);
    CURLcode err = curl_easy_setopt(curl_data->curl, CURLOPT_POSTFIELDS, body);
    return translate_curl_code(err);
}

GglError gghttplib_add_sigv4_credential(
    CurlData *curl_data, SigV4Details request_data
) {
    assert(curl_data != NULL);
    GglError err = GGL_ERR_OK;
    // scope to reduce stack size
    {
        // TODO: tune length based on longest-possible string
        // e.g. "aws:amz:us-gov-east-1:lambda" (30 characters)
        char sigv4_param[32];
        GglByteVec vector = GGL_BYTE_VEC(sigv4_param);
        ggl_byte_vec_chain_append(&err, &vector, GGL_STR("aws:amz:"));
        ggl_byte_vec_chain_append(&err, &vector, request_data.aws_region);
        ggl_byte_vec_chain_push(&err, &vector, ':');
        ggl_byte_vec_chain_append(&err, &vector, request_data.aws_service);
        ggl_byte_vec_chain_push(&err, &vector, '\0');
        if (err != GGL_ERR_OK) {
            GGL_LOGE("sigv4_param too small");
            return err;
        }
        CURLcode curl_err
            = curl_easy_setopt(curl_data->curl, CURLOPT_AWS_SIGV4, sigv4_param);
        if (curl_err != CURLE_OK) {
            return translate_curl_code(curl_err);
        }
    }

    // scope to reduce stack size
    {
        // "<128-chars>:<128-chars>"
        char sigv4_usrpwd[258];
        GglByteVec vector = GGL_BYTE_VEC(sigv4_usrpwd);
        ggl_byte_vec_chain_append(&err, &vector, request_data.access_key_id);
        ggl_byte_vec_chain_push(&err, &vector, ':');
        ggl_byte_vec_chain_append(
            &err, &vector, request_data.secret_access_key
        );
        ggl_byte_vec_chain_push(&err, &vector, '\0');
        if (err != GGL_ERR_OK) {
            GGL_LOGE("sigv4_usrpwd too small");
            return err;
        }
        CURLcode curl_err
            = curl_easy_setopt(curl_data->curl, CURLOPT_USERPWD, sigv4_usrpwd);
        if (curl_err != CURLE_OK) {
            return translate_curl_code(curl_err);
        }
    }

    return gghttplib_add_header(
        curl_data, GGL_STR("x-amz-security-token"), request_data.session_token
    );
}

GglError gghttplib_process_request(
    CurlData *curl_data, GglBuffer *response_buffer
) {
    assert(curl_data != NULL);
    GglByteVec response_vector = (response_buffer != NULL)
        ? ggl_byte_vec_init(*response_buffer)
        : (GglByteVec) { 0 };

    CURLcode curl_error = curl_easy_setopt(
        curl_data->curl, CURLOPT_HTTPHEADER, curl_data->headers_list
    );
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }
    if (response_buffer != NULL) {
        curl_error = curl_easy_setopt(
            curl_data->curl, CURLOPT_WRITEFUNCTION, write_response_to_buffer
        );
        if (curl_error != CURLE_OK) {
            return translate_curl_code(curl_error);
        }
        curl_error = curl_easy_setopt(
            curl_data->curl, CURLOPT_WRITEDATA, (void *) &response_vector
        );
        if (curl_error != CURLE_OK) {
            return translate_curl_code(curl_error);
        }
    }

    curl_error = curl_easy_perform(curl_data->curl);
    if (curl_error != CURLE_OK) {
        GGL_LOGE(
            "curl_easy_perform() failed: %s", curl_easy_strerror(curl_error)
        );
        if (curl_error == CURLE_WRITE_ERROR) {
            return GGL_ERR_NOMEM;
        }
        return translate_curl_code(curl_error);
    }

    long http_status_code = 0;
    curl_error = curl_easy_getinfo(
        curl_data->curl, CURLINFO_HTTP_CODE, &http_status_code
    );

    if (curl_error != CURLE_OK) {
        GGL_LOGE(
            "curl_easy_getinfo() failed: %s", curl_easy_strerror(curl_error)
        );
        return translate_curl_code(curl_error);
    }

    GGL_LOGI("HTTP code: %ld", http_status_code);

    if (response_buffer != NULL) {
        response_buffer->len = response_vector.buf.len;
    }

    // TODO: propagate HTTP code up for deployment failure root causing
    if (http_status_code < 200 || http_status_code > 299) {
        return GGL_ERR_FAILURE;
    }

    return translate_curl_code(curl_error);
}

GglError gghttplib_process_request_with_fd(CurlData *curl_data, int fd) {
    CURLcode curl_error = curl_easy_setopt(
        curl_data->curl, CURLOPT_HTTPHEADER, curl_data->headers_list
    );
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }
    curl_error = curl_easy_setopt(
        curl_data->curl, CURLOPT_WRITEFUNCTION, write_response_to_fd
    );
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }
    // coverity[bad_sizeof]
    curl_error
        = curl_easy_setopt(curl_data->curl, CURLOPT_WRITEDATA, (void *) &fd);
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }
    curl_error = curl_easy_setopt(curl_data->curl, CURLOPT_FAILONERROR, 1L);
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }

    curl_error = curl_easy_perform(curl_data->curl);
    if (curl_error != CURLE_OK) {
        GGL_LOGE(
            "curl_easy_perform() failed: %s", curl_easy_strerror(curl_error)
        );
        return translate_curl_code(curl_error);
    }

    long http_status_code = 0;
    curl_easy_getinfo(curl_data->curl, CURLINFO_HTTP_CODE, &http_status_code);
    GGL_LOGI("HTTP code: %ld", http_status_code);

    // TODO: propagate HTTP code up for deployment failure root causing
    return translate_curl_code(curl_error);
}
