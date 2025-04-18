// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "aws_sigv4.h"
#include "ggl/http.h"
#include "sigv4.h"
#include <assert.h>
#include <ggl/buffer.h>
#include <ggl/vector.h>
#include <openssl/evp.h>
#include <openssl/types.h>
#include <sys/time.h>
#include <time.h>
#include <stdint.h>

static int32_t hash_init(void *ctx) {
    // Initialize OpenSSL digest
    int32_t ret;

    if (EVP_DigestInit(ctx, EVP_sha256()) == 1) {
        // DigestInit returns 1 on success. But, sigv4 uses 0 as a success
        // value.
        ret = 0;
    } else {
        ret = -1;
    }

    return ret;
}

static int32_t hash_update(void *ctx, const uint8_t *data, size_t data_len) {
    int32_t ret;
    ret = EVP_DigestUpdate(ctx, data, data_len);

    if (ret == 1) {
        // DigestUpdate returns 1 on success. But, sigv4 uses 0 as a success
        // value.
        ret = 0;
    } else {
        ret = -1;
    }
    return ret;
}

static int32_t hash_final(
    void *ctx, uint8_t *output_buf, size_t output_buf_len
) {
    unsigned int out_buf_len = (unsigned int) output_buf_len;

    int32_t ret = EVP_DigestFinal(ctx, output_buf, &out_buf_len);

    if (ret == 1) {
        // DigestFinal returns 1 on success. But, sigv4 uses 0 as a success
        // value.
        ret = 0;
    } else {
        ret = -1;
    }

    return ret;
}

static GglError translate_sigv4_error(SigV4Status_t status) {
    GglError ret;

    switch (status) {
    case SigV4Success:
        ret = GGL_ERR_OK;
        break;

    case SigV4InvalidParameter:
        ret = GGL_ERR_INVALID;
        break;

    case SigV4InsufficientMemory:
        ret = GGL_ERR_NOMEM;
        break;

    case SigV4ISOFormattingError:
        ret = GGL_ERR_FAILURE;
        break;

    case SigV4MaxHeaderPairCountExceeded:
    // Fall through.
    case SigV4MaxQueryPairCountExceeded:
        ret = GGL_ERR_RANGE;
        break;

    case SigV4HashError:
        ret = GGL_ERR_FAILURE;
        break;

    case SigV4InvalidHttpHeaders:
        ret = GGL_ERR_INVALID;
        break;

    default:
        ret = GGL_ERR_FAILURE;
        break;
    }

    return ret;
}

static GglError aws_sigv4_generate_header(
    GglBuffer path,
    SigV4Details sigv4_details,
    GglBuffer http_headers,
    GglBuffer *auth_header,
    GglBuffer payload,
    GglBuffer http_method,
    GglBuffer query
) {
    char timestamp[17]; // YYYYMMDDTHHMMSSz\0
    EVP_MD_CTX *md_context = EVP_MD_CTX_new();

    SigV4HttpParameters_t http_params
        = { .pHeaders = (const char *) http_headers.data,
            .headersLen = http_headers.len,
            .pPayload = (const char *) payload.data,
            .payloadLen = payload.len,
            .flags = 0,
            .pHttpMethod = (const char *) http_method.data,
            .httpMethodLen = http_method.len,
            .pPath = (const char *) path.data,
            .pathLen = path.len,
            .pQuery = (const char *) query.data,
            .queryLen = query.len };

    SigV4CryptoInterface_t crypto_interface = {
        .hashInit = hash_init,
        .hashFinal = hash_final,
        .hashUpdate = hash_update,
        .pHashContext = md_context,
        .hashBlockLen = 0U,
        .hashDigestLen = 0U,
    };

    // Assign the block and digest length to the crypto interface.
    const size_t HASH_BLOCK_LEN = (size_t) EVP_MD_get_block_size(EVP_sha256());
    const size_t HASH_DIGEST_LEN = (size_t) EVP_MD_get_size(EVP_sha256());
    crypto_interface.hashDigestLen = HASH_DIGEST_LEN;
    crypto_interface.hashBlockLen = HASH_BLOCK_LEN;

    aws_sigv4_get_iso8601_time(timestamp, sizeof(timestamp));

    SigV4Credentials_t credentials
        = { .pAccessKeyId = (const char *) sigv4_details.access_key_id.data,
            .accessKeyIdLen = sigv4_details.access_key_id.len,
            .pSecretAccessKey
            = (const char *) sigv4_details.secret_access_key.data,
            .secretAccessKeyLen = sigv4_details.secret_access_key.len };

    const SigV4Parameters_t PARAMS = {
        .pRegion = (const char *) sigv4_details.aws_region.data,
        .regionLen = sigv4_details.aws_region.len,
        .pService = (const char *) sigv4_details.aws_service.data,
        .serviceLen = sigv4_details.aws_service.len,
        .pCredentials = &credentials,
        .pAlgorithm = SIGV4_AWS4_HMAC_SHA256,
        .algorithmLen = SIGV4_AWS4_HMAC_SHA256_LENGTH,
        .pHttpParameters = &http_params,
        .pCryptoInterface = &crypto_interface,
        .pDateIso8601 = timestamp,
    };

    uint8_t *signature;
    size_t signature_len;

    SigV4Status_t status = SigV4_GenerateHTTPAuthorization(
        &PARAMS,
        (char *) auth_header->data,
        &auth_header->len,
        (char **) &signature,
        &signature_len
    );

    // Free the context. We do not need this anymore.
    EVP_MD_CTX_free(md_context);

    return translate_sigv4_error(status);
}

size_t aws_sigv4_get_iso8601_time(char *buffer, size_t len) {
    assert(buffer != NULL);
    assert(len >= 17);
    struct timeval tv;
    struct tm tm_info;

    if (gettimeofday(&tv, NULL) != 0) {
        // Return an error to the caller.
        return 0;
    }

    gmtime_r(&tv.tv_sec, &tm_info);

    return strftime(buffer, 17, "%Y%m%dT%H%M%SZ", &tm_info);
}

GglError aws_sigv4_add_header_for_signing(
    GglByteVec *vector, GglBuffer header_key, GglBuffer header_value
) {
    GglError ret = ggl_byte_vec_append(vector, header_key);
    ggl_byte_vec_chain_push(&ret, vector, ':');
    ggl_byte_vec_chain_append(&ret, vector, header_value);

    // Non-canonical delimiter used by the AWS Sigv4 library to separate
    // different key:value pairs.
    ggl_byte_vec_chain_append(&ret, vector, GGL_STR("\r\n"));

    return ret;
}

GglError aws_sigv4_s3_get_create_header(
    GglBuffer filepath,
    SigV4Details sigv4_details,
    S3RequiredHeaders required_headers,
    GglByteVec *headers_to_sign,
    GglBuffer *auth_header
) {
    GglError err;

    assert(required_headers.host.len > 0);
    assert(required_headers.amz_content_sha256.len > 0);
    assert(required_headers.amz_date.len > 0);
    assert(required_headers.amz_security_token.len > 0);
    assert(headers_to_sign != NULL);
    assert(auth_header != NULL);
    assert(auth_header->len > 64U);

    err = aws_sigv4_add_header_for_signing(
        headers_to_sign,
        (GglBuffer) { .data = (uint8_t *) "host", .len = sizeof("host") - 1 },
        required_headers.host
    );

    if (err == GGL_ERR_OK) {
        err = aws_sigv4_add_header_for_signing(
            headers_to_sign,
            (GglBuffer) { .data = (uint8_t *) "x-amz-security-token",
                          .len = sizeof("x-amz-security-token") - 1 },
            required_headers.amz_security_token
        );
    }

    if (err == GGL_ERR_OK) {
        err = aws_sigv4_add_header_for_signing(
            headers_to_sign,
            (GglBuffer) { .data = (uint8_t *) "x-amz-date",
                          .len = sizeof("x-amz-date") - 1 },
            required_headers.amz_date
        );
    }

    if (err == GGL_ERR_OK) {
        err = aws_sigv4_add_header_for_signing(
            headers_to_sign,
            (GglBuffer) { .data = (uint8_t *) "x-amz-content-sha256",
                          .len = sizeof("x-amz-content-sha256") - 1 },
            required_headers.amz_content_sha256
        );
    }

    if (err == GGL_ERR_OK) {
        GglBuffer all_headers_to_sign = { .data = headers_to_sign->buf.data,
                                          .len = headers_to_sign->buf.len };
        // The payload should be empty for S3.
        err = aws_sigv4_generate_header(
            filepath,
            sigv4_details,
            all_headers_to_sign,
            auth_header,
            (GglBuffer) { .data = NULL, .len = 0 },
            GGL_STR("GET"),
            (GglBuffer) { .data = NULL, .len = 0 }
        );
    }

    return err;
}
