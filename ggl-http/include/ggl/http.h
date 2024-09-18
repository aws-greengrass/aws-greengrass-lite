// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIB_H
#define GGHTTPLIB_H

#include <ggl/object.h>

typedef struct CertificateDetails {
    const char *gghttplib_cert_path;
    const char *gghttplib_p_key_path;
    const char *gghttplib_root_ca_path;
} CertificateDetails;

/// AWS Service information and temporary credentials
///
/// Use fetch_token() to retrieve id, key, and token
typedef struct SigV4Details {
    /// AWS region code (e.g. "us-east-2")
    GglBuffer aws_region;
    /// AWS service endpoint name (e.g. "s3" or "lambda")
    GglBuffer aws_service;
    /// Temporary AWS ID
    GglBuffer access_key_id;
    /// Temporary AWS Key
    GglBuffer secret_access_key;
    /// Temporary AWS Token
    GglBuffer session_token;
} SigV4Details;

/// @brief Fetches temporary AWS credentials.
///
/// @param[in] url_for_token The aws IoT credentials endpoint URL.
/// @param[in] thing_name The name of the thing for which the token is being
/// fetched.
/// @param[in] certificate_details The certificate and private kye details to be
/// used for authentication.
/// @param[in] GglBuffer containing the fetched token.
///
/// @return void
///
/// This function sends a request to the IoT credentials endpoint URL using the
/// provided certificate and private keys details to authenticate the request.
/// The response containing the temporary credentials from the server is
/// expected to contain a token, which is returned as a GglBuffer object.
///
/// @note The called need to make sure that the paths of these certificates are
/// accessible in general without special privileges.
void fetch_token(
    const char *url_for_token,
    GglBuffer thing_name,
    CertificateDetails certificate_details,
    GglBuffer *buffer
);

/// @brief Downloads the content from the specified URL and saves it to the
/// given file path.
///
/// @param[in] url_for_generic_download The URL from which to fetch the content.
/// @param[in] file_path The local path to the file where the downloaded content
/// should be saved.
///
/// This function makes a GET request to the specified URL to download the
/// content.The downloaded content is then saved to the file specified by the
/// `file_path` parameter.
///
/// @note This function assumes that the necessary permissions are granted to
/// create or overwrite the file at the specified `file_path`.
///
/// @warning This function does not perform any validation or sanitization of
/// the input parameters. It is the responsibility of the caller to ensure that
/// the
///          provided `url_for_generic_download` and `file_path` are valid.
void generic_download(
    const char *url_for_generic_download, const char *file_path
);

#endif
