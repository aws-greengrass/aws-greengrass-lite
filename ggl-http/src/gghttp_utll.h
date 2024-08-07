// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIB_UTIL_H
#define GGHTTPLIB_UTIL_H

#include "ggl/http.h"

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
 */
void gghttplib_add_header(
    CurlData *curl_data, const char header_key[], const char *header_value
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
 * @brief Processes an HTTP request using the provided cURL data.
 *
 * This function sets up the CURL handle with the necessary options, performs
 * the HTTP request, and writes the response to a buffer.
 *
 * @param[in] curl_data A pointer to the CurlData struct containing the cURL
 * handle and other request data.
 * @return A GglBuffer struct containing the response data from the HTTP
 * request.
 */
GglBuffer gghttplib_process_request(CurlData *curl_data);

#endif
