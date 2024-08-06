// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIB_H
#define GGHTTPLIB_H

#include "ggl/object.h"
#include <curl/curl.h>
#include <ggl/error.h>

typedef struct CertificateDetails {
    char *gghttplib_cert_path;
    char *gghttplib_p_key_path;
    char *gghttplib_root_ca_path;
} CertificateDetails;

/**
 * @brief Fetches temporary AWS credentials.
 *
 * @param[in] url_for_token The aws IoT credentials endpoint URL.
 * @param[in] thing_name The name of the thing for which the token is being
 * fetched.
 * @param[in] certificate_details The certificate and private kye details to be
 * used for authentication.
 *
 * @return GglBuffer containing the fetched token.
 *
 * This function sends a request to the IoT credentials endpoint URL using the
 * provided certificate and private keys details to authenticate the request.
 * The response containing the temporary credentials from the server is expected
 * to contain a token, which is returned as a GglBuffer object.
 *
 * @note The called need to make sure that the paths of these certificates are
 * accessible in general without special privileges.
 *
 * @note The caller is responsible for freeing the memory associated with the
 *       returned GglBuffer object -> data member.
 */
GglBuffer fetch_token(
    const char *url_for_token,
    const char *thing_name,
    CertificateDetails certificate_details
);

/**
 * @brief Downloads the content from the specified URL and saves it to the given
 * file path.
 *
 * @param[in] url_for_generic_download The URL from which to fetch the content.
 * @param[in] file_path The local path to the file where the downloaded content
 * should be saved.
 *
 * This function makes a GET request to the specified URL to download the
 * content.The downloaded content is then saved to the file specified by the
 * `file_path` parameter.
 *
 * @note This function assumes that the necessary permissions are granted to
 * create or overwrite the file at the specified `file_path`.
 *
 * @warning This function does not perform any validation or sanitization of the
 * input parameters. It is the responsibility of the caller to ensure that the
 *          provided `url_for_generic_download` and `file_path` are valid.
 */
void generic_download(
    const char *url_for_generic_download, const char *file_path
);

#endif
