// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tls.h"
#include "ggl/error.h"
#include "ggl/log.h"
#include "iotcored.h"
#include <assert.h>
#include <ggl/arena.h>
#include <ggl/buffer.h>
#include <ggl/uri.h>
#include <limits.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/http.h>
#include <openssl/ssl.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

// RFC 1035 specifies 255 max octets.
// 2 octets are reserved for length and trailing dot which are not encoded here
#define MAX_DNS_NAME_LEN 253
#define MAX_PORT_LENGTH 5
#define MAX_SCHEME_LENGTH (sizeof("https://") - 1)
#define MAX_USERINFO_LENGTH \
    (PATH_MAX - MAX_DNS_NAME_LEN - MAX_PORT_LENGTH - MAX_SCHEME_LENGTH)

struct IotcoredTlsCtx {
    SSL_CTX *ssl_ctx;
    BIO *bio;
    bool connected;
};

IotcoredTlsCtx conn;

static int ssl_error_callback(const char *str, size_t len, void *user) {
    (void) user;
    // discard \n
    if (len > 0) {
        --len;
    }
    GGL_LOGE("[openssl]: %.*s", (int) len, str);
    return 1;
}

static GglError proxy_get_info(
    const IotcoredArgs *args, GglUriInfo *proxy_info
) {
    assert(args->endpoint != NULL);

    const char *proxy_uri = OSSL_HTTP_adapt_proxy(
        args->proxy_uri, args->no_proxy, args->endpoint, 1
    );
    if (proxy_uri == NULL) {
        GGL_LOGD("Connecting without proxy.");
        return GGL_ERR_OK;
    }

    static uint8_t uri_parse_mem[256];
    GglArena uri_alloc = ggl_arena_init(GGL_BUF(uri_parse_mem));
    GglUriInfo proxy_parsed = { 0 };
    GglError ret = gg_uri_parse(
        &uri_alloc, ggl_buffer_from_null_term((char *) proxy_uri), &proxy_parsed
    );
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to parse proxy URL.");
    }

    if (proxy_parsed.host.len == 0) {
        GGL_LOGE("No proxy host provided.");
        return GGL_ERR_INVALID;
    }
    if (proxy_parsed.host.len > MAX_DNS_NAME_LEN) {
        GGL_LOGE("Proxy host too long.");
        return GGL_ERR_NOMEM;
    }

    static uint8_t host_mem[MAX_DNS_NAME_LEN + 1];
    memcpy(host_mem, proxy_parsed.host.data, proxy_parsed.host.len);
    host_mem[proxy_parsed.host.len] = '\0';
    proxy_info->host.data = host_mem;

    if (proxy_parsed.port.len > MAX_PORT_LENGTH) {
        GGL_LOGE("Port provided too long.");
        return GGL_ERR_INVALID;
    }
    // Defaults retrieved from here:
    // https://docs.aws.amazon.com/greengrass/v2/developerguide/configure-greengrass-core-v2.html#network-proxy-object
    if (proxy_parsed.port.len == 0) {
        GGL_LOGI(
            "No proxy port provided, using 80/443 as default for http/https."
        );
    } else {
        static uint8_t proxy_port_mem[MAX_PORT_LENGTH + 1];
        memcpy(proxy_port_mem, proxy_parsed.port.data, proxy_parsed.port.len);
        proxy_port_mem[proxy_parsed.port.len] = '\0';
        proxy_info->port.data = proxy_port_mem;
    }

    if (proxy_parsed.userinfo.len > MAX_USERINFO_LENGTH) {
        GGL_LOGE("Proxy userinfo field too long; ignoring.");
        proxy_parsed.userinfo = GGL_STR("");
    } else if (proxy_parsed.userinfo.len > 0) {
        static uint8_t userinfo_mem[MAX_USERINFO_LENGTH + 1];
        memcpy(
            userinfo_mem, proxy_parsed.userinfo.data, proxy_parsed.userinfo.len
        );
        userinfo_mem[proxy_parsed.userinfo.len] = '\0';
        proxy_parsed.userinfo.data = userinfo_mem;
    }

    *proxy_info = proxy_parsed;
    return GGL_ERR_OK;
}

static GglError create_tls_context(
    const IotcoredArgs *args, SSL_CTX **ssl_ctx
) {
    assert(ssl_ctx != NULL);
    SSL_CTX *new_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (new_ssl_ctx == NULL) {
        GGL_LOGE("Failed to create openssl context.");
        return GGL_ERR_NOMEM;
    }

    SSL_CTX_set_verify(new_ssl_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_mode(new_ssl_ctx, SSL_MODE_AUTO_RETRY);

    if (SSL_CTX_load_verify_file(new_ssl_ctx, args->rootca) != 1) {
        GGL_LOGE("Failed to load root CA.");
        return GGL_ERR_CONFIG;
    }

    if (SSL_CTX_use_certificate_file(new_ssl_ctx, args->cert, SSL_FILETYPE_PEM)
        != 1) {
        GGL_LOGE("Failed to load client certificate.");
        return GGL_ERR_CONFIG;
    }

    if (SSL_CTX_use_PrivateKey_file(new_ssl_ctx, args->key, SSL_FILETYPE_PEM)
        != 1) {
        GGL_LOGE("Failed to load client private key.");
        return GGL_ERR_CONFIG;
    }

    if (SSL_CTX_check_private_key(new_ssl_ctx) != 1) {
        GGL_LOGE("Client certificate and private key do not match.");
        return GGL_ERR_CONFIG;
    }
    *ssl_ctx = new_ssl_ctx;
    return GGL_ERR_OK;
}

static GglError do_handshake(char *host, BIO *bio) {
    SSL *ssl;
    BIO_get_ssl(bio, &ssl);

    if (SSL_set_tlsext_host_name(ssl, host) != 1) {
        GGL_LOGE("Failed to configure SNI.");
        return GGL_ERR_FATAL;
    }

    if (SSL_do_handshake(ssl) != 1) {
        GGL_LOGE("Failed TLS handshake.");
        return GGL_ERR_FAILURE;
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        GGL_LOGE("Failed TLS server certificate verification.");
        return GGL_ERR_FAILURE;
    }

    return GGL_ERR_OK;
}

static GglError iotcored_tls_connect_no_proxy(
    const IotcoredArgs *args, IotcoredTlsCtx **ctx
) {
    SSL_CTX *ssl_ctx = NULL;
    GglError ret = create_tls_context(args, &ssl_ctx);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    BIO *bio = BIO_new_ssl_connect(ssl_ctx);
    if (bio == NULL) {
        GGL_LOGE("Failed to create openssl BIO.");
        return GGL_ERR_FATAL;
    }

    if (BIO_set_conn_port(bio, "8883") != 1) {
        GGL_LOGE("Failed to set port.");
        return GGL_ERR_FATAL;
    }

    if (BIO_set_conn_hostname(bio, args->endpoint) != 1) {
        GGL_LOGE("Failed to set hostname.");
        return GGL_ERR_FATAL;
    }

    ret = do_handshake(args->endpoint, bio);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    conn = (IotcoredTlsCtx
    ) { .ssl_ctx = ssl_ctx, .bio = bio, .connected = true };
    *ctx = &conn;

    return GGL_ERR_OK;
}

static GglError iotcored_proxy_connect_tunnel(
    const IotcoredArgs *args, GglUriInfo info, BIO *proxy_bio
) {
    char *proxy_user = NULL;
    char *proxy_password = NULL;
    // TODO: parse userinfo
    (void) info;
    // Tunnel to the IoT endpoint
    GGL_LOGD("Connecting through the http proxy.");
    int proxy_connect_ret = OSSL_HTTP_proxy_connect(
        proxy_bio,
        args->endpoint,
        "8883",
        proxy_user,
        proxy_password,
        120,
        NULL,
        NULL
    );
    if (proxy_connect_ret != 1) {
        GGL_LOGE("Failed http proxy connect.");
        return GGL_ERR_FAILURE;
    }
    return GGL_ERR_OK;
}

static GglError iotcored_tls_connect_https_proxy(
    const IotcoredArgs *args, IotcoredTlsCtx **ctx, GglUriInfo info
) {
    (void) args;
    (void) ctx;
    (void) info;
    // default fallback
    if (info.port.len == 0) {
        info.port = GGL_STR("443");
    }
    // TODO: support this.
    GGL_LOGE("HTTPS proxy unsupported.");
    return GGL_ERR_UNSUPPORTED;
}

static GglError iotcored_tls_connect_http_proxy(
    const IotcoredArgs *args, IotcoredTlsCtx **ctx, GglUriInfo info
) {
    // Set up TLS before attempting a connection
    SSL_CTX *ssl_ctx = NULL;
    GglError ret = create_tls_context(args, &ssl_ctx);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    BIO *mqtt_bio = BIO_new_ssl(ssl_ctx, 1);
    if (mqtt_bio == NULL) {
        GGL_LOGE("Failed to create openssl BIO.");
        return GGL_ERR_FATAL;
    }

    // default fallback
    if (info.port.len == 0) {
        info.port = GGL_STR("80");
    }

    // open a plain-text socket to talk with proxy
    BIO *proxy_bio = BIO_new(BIO_s_connect());
    if (proxy_bio == NULL) {
        GGL_LOGE("Failed to create proxy socket.");
        return GGL_ERR_FATAL;
    }
    if (BIO_set_conn_hostname(proxy_bio, info.host.data) != 1) {
        GGL_LOGE("Failed to set proxy hostname.");
        return GGL_ERR_FATAL;
    }
    if (BIO_set_conn_port(proxy_bio, info.port.data) != 1) {
        GGL_LOGE("Failed to set proxy port.");
        return GGL_ERR_FATAL;
    }
    GGL_LOGD("Connecting to HTTP proxy.");
    if (BIO_do_connect(proxy_bio) != 1) {
        GGL_LOGE("Failed to connect to proxy.");
        return GGL_ERR_FAILURE;
    }

    // Perform TLS with the IoT endpoint thru the tunnel
    ret = iotcored_proxy_connect_tunnel(args, info, proxy_bio);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    // The proxy connection is the source and sink for all SSL bytes.
    BIO *mqtt_proxy_chain = BIO_push(mqtt_bio, proxy_bio);
    ret = do_handshake(args->endpoint, mqtt_proxy_chain);
    if (ret != GGL_ERR_OK) {
        return ret;
    }

    conn = (IotcoredTlsCtx
    ) { .ssl_ctx = ssl_ctx, .bio = mqtt_proxy_chain, .connected = true };
    *ctx = &conn;

    return GGL_ERR_OK;
}

GglError iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx) {
    GglUriInfo info = { 0 };
    GglError ret = proxy_get_info(args, &info);
    if (ret != GGL_ERR_OK) {
        return ret;
    }
    if (info.host.len > 0) {
        if (ggl_buffer_eq(info.scheme, GGL_STR("https"))) {
            ret = iotcored_tls_connect_https_proxy(args, ctx, info);
        } else if ((info.scheme.len == 0)
                   || ggl_buffer_eq(info.scheme, GGL_STR("http"))) {
            ret = iotcored_tls_connect_http_proxy(args, ctx, info);
        } else {
            GGL_LOGE(
                "Unsupported scheme \"%.*s\".",
                (int) info.scheme.len,
                info.scheme.data
            );
        }
    } else {
        ret = iotcored_tls_connect_no_proxy(args, ctx);
    }

    if (ret != GGL_ERR_OK) {
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return ret;
    }

    GGL_LOGI("Successfully connected.");
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
        int error_code = SSL_get_error(ssl, ret);
        ERR_print_errors_cb(ssl_error_callback, NULL);
        switch (error_code) {
        case SSL_ERROR_SSL:
        case SSL_ERROR_SYSCALL:
            GGL_LOGE("Connection unexpectedly closed.");
            ctx->connected = false;
            buf->len = 0;
            return GGL_ERR_FATAL;
        case SSL_ERROR_ZERO_RETURN:
            GGL_LOGE("Unexpected EOF.");
            buf->len = 0;
            return GGL_ERR_FAILURE;
        default:
            // All other error codes are related to non-blocking sockets
            GGL_LOGW("Unexpected non-blocking socket error.");
            break;
        }
    }
    buf->len = read_bytes;
    return GGL_ERR_OK;
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
        int error_code = SSL_get_error(ssl, ret);
        ERR_print_errors_cb(ssl_error_callback, NULL);
        switch (error_code) {
        case SSL_ERROR_SSL:
        case SSL_ERROR_SYSCALL:
            GGL_LOGE("Connection unexpectedly closed.");
            ctx->connected = false;
            return GGL_ERR_FATAL;
        default:
            // All other error codes are related to non-blocking sockets
            // or cannot occur from a socket write.
            GGL_LOGW("Unexpected non-blocking socket error.");
            break;
        }
    }

    return 0;
}

void iotcored_tls_cleanup(IotcoredTlsCtx *ctx) {
    assert(ctx != NULL);

    // Freeing the SSL buffer may attempt to send the shutdown message
    // over a closed connection. This may happen when the buffer
    // is not the source/sink for network bytes (i.e. running through a proxy)
    // This results in an error we will just ignore for now...
    ERR_set_mark();
    if (ctx->bio != NULL) {
        BIO_free_all(ctx->bio);
    }
    if (ctx->ssl_ctx != NULL) {
        SSL_CTX_free(ctx->ssl_ctx);
    }
    ERR_clear_last_mark();

    (*ctx) = (IotcoredTlsCtx) { 0 };
}
