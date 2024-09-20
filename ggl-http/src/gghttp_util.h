// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIB_UTIL_H
#define GGHTTPLIB_UTIL_H

#include "ggl/http.h"
#include <curl/curl.h>
#include <ggl/error.h>
#include <ggl/object.h>
#include <stdio.h>

typedef struct CurlData {
    CURL *curl;
    struct curl_slist *headers_list;
} CurlData;

/**
 * @brief Initializes a CURL handle and sets the URL for the HTTP request.
 *
 * @param[in] curl_data A pointer to a CurlData structure that will hold the
 * CURL handle and headers.
 * @param[in] url The URL for the HTTP request.
 *
 * @return GGL_ERR_OK on success, or GGL_ERR_FAILURE if the CURL handle cannot
 * be created.
 *
 * This function initializes a CURL handle and sets the URL for the HTTP
 * request. The CURL handle is stored in the `curl` member of the `curl_data`
 * structure, and the `headers_list` member is initialized to NULL.
 *
 * If the CURL handle cannot be created, an error message is logged, and the
 * function returns GGL_ERR_FAILURE.
 */
GglError gghttplib_init_curl(CurlData *curl_data, const char *url);

/**
 * @brief Adds a header to the list of headers for the CURL request.
 *
 * @param[in] curl_data The CurlData object containing the CURL handle and
 * headers list.
 * @param[in] header_key The key of the header to be added.
 * @param[in] header_value The value of the header to be added.
 * @return GGL_ERR_OK on success, else an error value on failure
 * @note curl_data is unmodified on failure.
 */
GglError gghttplib_add_header(
    CurlData *curl_data, GglBuffer header_key, GglBuffer header_value
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
void gghttplib_add_certificate_data(
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
void gghttplib_add_post_body(CurlData *curl_data, const char *body);

/// @brief Adds AWS Signature Version 4 to the CURL handle.
///
/// This function sets the access key id, secret access key, session token
/// for SigV4 the CURL handle
///
/// @param[in] curl_data A pointer to the CurlData struct containing the cURL
/// handle.
/// @param[in] request_data a SigV4Details struct containing the relevant
/// temporary credentials, vended by the IoT Credentials endpoint.
/// @return If the CURL data cannot be created, GGL_ERR_NOMEM, else GGL_ERR_OK
GglError gghttplib_add_sigv4_credential(
    CurlData *curl_data, SigV4Details request_data
);

/// @brief Processes an HTTP request using the provided cURL data.
///
/// This function sets up the CURL handle with the necessary options, performs
/// the HTTP request, and writes the response to a buffer.
///
/// @param[in] curl_data A pointer to the CurlData struct containing the cURL
/// handle and other request data.
/// @return A GglBuffer struct containing the response data from the HTTP
/// request.
GglError gghttplib_process_request(
    CurlData *curl_data, GglBuffer *response_buffer
);

/// @brief Processes an HTTP request using the provided cURL data.
///
/// This function sets up the CURL handle with the necessary options, performs
/// the HTTP request, and writes the response to a file pointer.
///
/// @param[in] curl_data A pointer to the CurlData struct containing the cURL
/// handle and other request data.
/// @param[in] file_pointer A file pointer to the write the data to
/// @return A GglError for success status report
GglError gghttplib_process_request_with_file_pointer(
    CurlData *curl_data, FILE *file_pointer
);

#endif
