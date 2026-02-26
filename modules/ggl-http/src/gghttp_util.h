// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIB_UTIL_H
#define GGHTTPLIB_UTIL_H

#include <curl/curl.h>
#include <gg/error.h>
#include <gg/types.h>
#include <ggl/http.h>

typedef struct TpmCallbackData {
    const char *key_path;
    const char *cert_path;
} TpmCallbackData;

typedef struct CurlData {
    CURL *curl;
    struct curl_slist *headers_list;
    TpmCallbackData tpm_data;
} CurlData;

/**
 * @brief Initializes a CURL handle and sets the URL for the HTTP request.
 *
 * @param[in] curl_data A pointer to a CurlData structure that will hold the
 * CURL handle and headers.
 * @param[in] url The URL for the HTTP request.
 *
 * @return GG_ERR_OK on success, or GGL_ERR_FAILURE if the CURL handle cannot
 * be created.
 *
 * This function initializes a CURL handle and sets the URL for the HTTP
 * request. The CURL handle is stored in the `curl` member of the `curl_data`
 * structure, and the `headers_list` member is initialized to NULL.
 *
 * If the CURL handle cannot be created, an error message is logged, and the
 * function returns GG_ERR_FAILURE.
 */
GgError gghttplib_init_curl(CurlData *curl_data, const char *url);

void gghttplib_destroy_curl(CurlData *curl_data);

/**
 * @brief Adds a header to the list of headers for the CURL request.
 *
 * @param[in] curl_data The CurlData object containing the CURL handle and
 * headers list.
 * @param[in] header_key The key of the header to be added.
 * @param[in] header_value The value of the header to be added.
 * @return GG_ERR_OK on success, else an error value on failure
 * @note curl_data is unmodified on failure.
 */
GgError gghttplib_add_header(
    CurlData *curl_data, GgBuffer header_key, GgBuffer header_value
);

/**
 * @brief Adds certificate data to the CURL handle.
 *
 * This function sets the certificate, private key, and root CA path options
 * for the cURL handle using the provided CertificateDetails struct.
 *
 * @param[in] curl_data A pointer to the CurlData struct containing the cURL
 * handle.
 * @param[in] request_data A CertificateDetails struct containing the paths to
 * the certificate, private key, and root CA files.
 */
GgError gghttplib_add_certificate_data(
    CurlData *curl_data, CertificateDetails request_data
);

/**
 * @brief Adds a body to the CURL request, which also makes it a POST request.
 *
 * This function sets the CURL postfields field to the provided body.
 *
 * @param[in] curl_data A pointer to the CurlData struct containing the cURL
 * handle.
 * @param[in] body The content to be added to the request in the body.
 */
GgError gghttplib_add_post_body(CurlData *curl_data, const char *body);

/// @brief Processes an HTTP request using the provided cURL data.
///
/// This function sets up the CURL handle with the necessary options, performs
/// the HTTP request, and writes the response to a buffer.
///
/// @param[in] curl_data A pointer to the CurlData struct containing the cURL
/// handle and other request data.
/// @return A GgBuffer struct containing the response data from the HTTP
/// request.
GgError gghttplib_process_request(
    CurlData *curl_data, GgBuffer *response_buffer
);

/// @brief Processes an HTTP request using the provided cURL data.
///
/// This function sets up the CURL handle with the necessary options, performs
/// the HTTP request, and writes the response to a file descriptor.
///
/// @param[in] curl_data A pointer to the CurlData struct containing the cURL
/// handle and other request data.
/// @param[in] fd A file descriptor to the write the data to
/// @return A GgError for success status report
GgError gghttplib_process_request_with_fd(CurlData *curl_data, int fd);

#endif
