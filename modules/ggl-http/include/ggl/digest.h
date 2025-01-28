// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGHTTPLIB_DIGEST_H
#define GGHTTPLIB_DIGEST_H

#include <ggl/buffer.h>
#include <ggl/error.h>
#include <openssl/types.h>

typedef struct GglDigest {
    EVP_MD_CTX *ctx;
} GglDigest;

GglDigest ggl_new_digest(GglError *error);

/// @brief Verifies a file's contents using SHA256.
///
/// @param[in] dirfd director to read from
/// @param[in] path path from the directory to the file to verify
/// @param[in] expected_digest the SHA256 hash expected for the contents of the
/// file descriptor. fetched.
/// @param[in] digest_context context initialized by ggl_new_digest(), used by
/// underlying digest algorithm.
///
/// @return error code on failure, GGL_ERR_OK on success.
///
/// @note digest_context may be reused for subsequent digests.
GglError ggl_verify_sha256_digest(
    int dirfd,
    GglBuffer path,
    GglBuffer expected_digest,
    GglDigest digest_context
);

void ggl_free_digest(GglDigest *digest_context);

#endif
