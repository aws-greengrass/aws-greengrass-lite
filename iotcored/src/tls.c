/* aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "tls.h"
#include "args.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "ggl/object.h"
#include <assert.h>
#include <openssl/bio.h>
#include <openssl/ssl.h>
#include <openssl/types.h>
#include <stdbool.h>
#include <stdlib.h>

struct IotcoredTlsCtx {
    SSL_CTX *ssl_ctx;
    BIO *bio;
    bool connected;
};

IotcoredTlsCtx conn;

GglError iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx) {
    assert(ctx != NULL);

    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        GGL_LOGE("ssl", "Failed to create openssl context.");
        return GGL_ERR_NOMEM;
    }

    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

    if (SSL_CTX_load_verify_file(ssl_ctx, args->rootca) != 1) {
        GGL_LOGE("ssl", "Failed to load root CA.");
        return GGL_ERR_CONFIG;
    }

    if (SSL_CTX_use_certificate_file(ssl_ctx, args->cert, SSL_FILETYPE_PEM)
        != 1) {
        GGL_LOGE("ssl", "Failed to load client certificate.");
        return GGL_ERR_CONFIG;
    }

    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, args->key, SSL_FILETYPE_PEM)
        != 1) {
        GGL_LOGE("ssl", "Failed to load client private key.");
        return GGL_ERR_CONFIG;
    }

    if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
        GGL_LOGE("ssl", "Client certificate and private key do not match.");
        return GGL_ERR_CONFIG;
    }

    BIO *bio = BIO_new_ssl_connect(ssl_ctx);
    if (bio == NULL) {
        GGL_LOGE("ssl", "Failed to create openssl BIO.");
        return GGL_ERR_FATAL;
    }

    if (BIO_set_conn_port(bio, "8883") != 1) {
        GGL_LOGE("ssl", "Failed to set port.");
        return GGL_ERR_FATAL;
    }

    if (BIO_set_conn_hostname(bio, args->endpoint) != 1) {
        GGL_LOGE("ssl", "Failed to set hostname.");
        return GGL_ERR_FATAL;
    }

    SSL *ssl;
    BIO_get_ssl(bio, &ssl);

    if (SSL_set_tlsext_host_name(ssl, args->endpoint) != 1) {
        GGL_LOGE("ssl", "Failed to configure SNI.");
        return GGL_ERR_FATAL;
    }

    if (SSL_do_handshake(ssl) != 1) {
        GGL_LOGE("ssl", "Failed TLS handshake.");
        return GGL_ERR_FAILURE;
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        GGL_LOGE("ssl", "Failed TLS server certificate verification.");
        return GGL_ERR_FAILURE;
    }

    conn = (IotcoredTlsCtx) {
        .ssl_ctx = ssl_ctx,
        .bio = bio,
        .connected = true,
    };

    *ctx = &conn;

    GGL_LOGI("ssl", "Successfully connected.");
    return 0;
}

GglError iotcored_tls_read(IotcoredTlsCtx *ctx, GglBuffer *buf) {
    assert(ctx != NULL);
    assert(buf != NULL);

    if (!ctx->connected) {
        return GGL_ERR_NOCONN;
    }

    SSL *ssl;
    BIO_get_ssl(ctx->bio, &ssl);

    size_t read_bytes = 0;
    int ret = SSL_read_ex(ssl, buf->data, buf->len, &read_bytes);

    if (ret != 1) {
        GGL_LOGE("ssl", "Read failed.");
        return GGL_ERR_FAILURE;
    }

    buf->len = read_bytes;
    return 0;
}

GglError iotcored_tls_write(IotcoredTlsCtx *ctx, GglBuffer buf) {
    assert(ctx != NULL);

    if (!ctx->connected) {
        return GGL_ERR_NOCONN;
    }

    SSL *ssl;
    BIO_get_ssl(ctx->bio, &ssl);

    size_t written;
    int ret = SSL_write_ex(ssl, buf.data, buf.len, &written);

    if (ret != 1) {
        GGL_LOGE("ssl", "Write failed.");
        return GGL_ERR_FAILURE;
    }

    return 0;
}

void iotcored_tls_cleanup(IotcoredTlsCtx *ctx) {
    assert(ctx != NULL);

    if (ctx->bio != NULL) {
        BIO_ssl_shutdown(ctx->bio);
        BIO_free_all(ctx->bio);
    }
    if (ctx->ssl_ctx != NULL) {
        SSL_CTX_free(ctx->ssl_ctx);
    }
    ctx->connected = false;
}
