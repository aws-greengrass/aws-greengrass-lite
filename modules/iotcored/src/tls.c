// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "tls.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <ggl/uri.h>
#include <iotcored.h>
#include <limits.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/http.h>
#include <openssl/prov_ssl.h>
#include <openssl/ssl.h>
#include <openssl/store.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
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

static pthread_mutex_t ssl_mtx = PTHREAD_MUTEX_INITIALIZER;

static GgError make_nonblocking(BIO *bio) {
    int fd = -1;
    BIO_get_fd(bio, &fd);
    if (fd < 0) {
        GG_LOGE("Failed to get socket fd from BIO.");
        return GG_ERR_FAILURE;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    if ((flags == -1) || (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)) {
        GG_LOGE("Failed to set socket non-blocking: %m.");
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

int iotcored_tls_get_fd(IotcoredTlsCtx *ctx) {
    if ((ctx == NULL) || !ctx->connected) {
        return -1;
    }
    int fd = -1;
    BIO_get_fd(ctx->bio, &fd);
    return fd;
}

bool iotcored_tls_read_ready(IotcoredTlsCtx *ctx) {
    if ((ctx == NULL) || !ctx->connected) {
        return false;
    }
    SSL *ssl = NULL;
    BIO_get_ssl(ctx->bio, &ssl);
    GG_MTX_SCOPE_GUARD(&ssl_mtx);
    return (ssl != NULL) && (SSL_has_pending(ssl) != 0);
}

static int ssl_error_callback(const char *str, size_t len, void *user) {
    (void) user;
    // discard \n
    if (len > 0) {
        --len;
    }
    GG_LOGE("openssl: %.*s", (int) len, str);
    return 1;
}

static GgError proxy_get_info(
    const IotcoredArgs *args, GglUriInfo *proxy_info
) {
    assert(args->endpoint != NULL);

    const char *proxy_uri = OSSL_HTTP_adapt_proxy(
        args->proxy_uri, args->no_proxy, args->endpoint, 1
    );
    if (proxy_uri == NULL) {
        GG_LOGD("Connecting without proxy.");
        return GG_ERR_OK;
    }

    static uint8_t uri_parse_mem[256];
    GgArena uri_alloc = gg_arena_init(GG_BUF(uri_parse_mem));
    GglUriInfo proxy_parsed = { 0 };
    GgError ret = gg_uri_parse(
        &uri_alloc, gg_buffer_from_null_term((char *) proxy_uri), &proxy_parsed
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse proxy URL.");
    }

    if (proxy_parsed.host.len == 0) {
        GG_LOGE("No proxy host provided.");
        return GG_ERR_INVALID;
    }
    if (proxy_parsed.host.len > MAX_DNS_NAME_LEN) {
        GG_LOGE("Proxy host too long.");
        return GG_ERR_NOMEM;
    }

    static uint8_t host_mem[MAX_DNS_NAME_LEN + 1];
    memcpy(host_mem, proxy_parsed.host.data, proxy_parsed.host.len);
    host_mem[proxy_parsed.host.len] = '\0';
    proxy_info->host.data = host_mem;

    if (proxy_parsed.port.len > MAX_PORT_LENGTH) {
        GG_LOGE("Port provided too long.");
        return GG_ERR_INVALID;
    }
    // Defaults retrieved from here:
    // https://docs.aws.amazon.com/greengrass/v2/developerguide/configure-greengrass-core-v2.html#network-proxy-object
    if (proxy_parsed.port.len == 0) {
        GG_LOGI(
            "No proxy port provided, using 80/443 as default for http/https."
        );
    } else {
        static uint8_t proxy_port_mem[MAX_PORT_LENGTH + 1];
        memcpy(proxy_port_mem, proxy_parsed.port.data, proxy_parsed.port.len);
        proxy_port_mem[proxy_parsed.port.len] = '\0';
        proxy_info->port.data = proxy_port_mem;
    }

    if (proxy_parsed.userinfo.len > MAX_USERINFO_LENGTH) {
        GG_LOGE("Proxy userinfo field too long; ignoring.");
        proxy_parsed.userinfo = GG_STR("");
    } else if (proxy_parsed.userinfo.len > 0) {
        static uint8_t userinfo_mem[MAX_USERINFO_LENGTH + 1];
        memcpy(
            userinfo_mem, proxy_parsed.userinfo.data, proxy_parsed.userinfo.len
        );
        userinfo_mem[proxy_parsed.userinfo.len] = '\0';
        proxy_parsed.userinfo.data = userinfo_mem;
    }

    *proxy_info = proxy_parsed;
    return GG_ERR_OK;
}

static void check_ktls_status(SSL *ssl) {
    BIO *wbio = SSL_get_wbio(ssl);
    BIO *rbio = SSL_get_rbio(ssl);
    // Suppress unused warnings - _FORTIFY_SOURCE may optimize away variable
    // usage
    (void) wbio;
    (void) rbio;

    int tx_status = BIO_get_ktls_send(wbio);
    int rx_status = BIO_get_ktls_recv(rbio);

    GG_LOGD("kTLS TX status: %d, RX status: %d", tx_status, rx_status);

    if (BIO_get_ktls_send(wbio) < 1) {
        GG_LOGW("kTLS Tx is not fully active.");
    }

    if (BIO_get_ktls_recv(rbio) < 1) {
        GG_LOGW("kTLS Rx is not fully active.");
    }
}

static void try_enable_ktls(SSL_CTX *ssl_ctx) {
    // Set the minimum protocol version to TLS 1.2
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);

    // Enable kTLS on the ctx
    SSL_CTX_set_options(ssl_ctx, SSL_OP_ENABLE_KTLS);
    if (!(SSL_CTX_get_options(ssl_ctx) & SSL_OP_ENABLE_KTLS)) {
        GG_LOGW("Failed to enable kTLS option on SSL ctx.");
    }

    GG_LOGT("kTLS option set on SSL context.");
}

static GgError load_cert_from_uri(SSL_CTX *ssl_ctx, const char *uri) {
    OSSL_STORE_CTX *store_ctx = OSSL_STORE_open(uri, NULL, NULL, NULL, NULL);
    if (store_ctx == NULL) {
        GG_LOGE("Failed to open cert store.");
        return GG_ERR_NOMEM;
    }

    OSSL_STORE_INFO *info = OSSL_STORE_load(store_ctx);
    if (info == NULL) {
        GG_LOGE("Failed to load cert info.");
        OSSL_STORE_close(store_ctx);
        return GG_ERR_CONFIG;
    }

    X509 *cert = OSSL_STORE_INFO_get1_CERT(info);
    OSSL_STORE_INFO_free(info);
    OSSL_STORE_close(store_ctx);

    if (cert == NULL) {
        GG_LOGE("Failed to extract certificate.");
        return GG_ERR_CONFIG;
    }

    if (SSL_CTX_use_certificate(ssl_ctx, cert) != 1) {
        GG_LOGE("Failed to use certificate.");
        X509_free(cert);
        return GG_ERR_CONFIG;
    }

    X509_free(cert);
    return GG_ERR_OK;
}

static GgError load_key_from_uri(SSL_CTX *ssl_ctx, const char *uri) {
    OSSL_STORE_CTX *store_ctx = OSSL_STORE_open(uri, NULL, NULL, NULL, NULL);
    if (store_ctx == NULL) {
        GG_LOGE("Failed to open key store.");
        return GG_ERR_NOMEM;
    }

    EVP_PKEY *pkey = NULL;
    while (!OSSL_STORE_eof(store_ctx)) {
        OSSL_STORE_INFO *info = OSSL_STORE_load(store_ctx);
        if (info == NULL) {
            GG_LOGE("Failed to load key info.");
            OSSL_STORE_close(store_ctx);
            return GG_ERR_CONFIG;
        }

        if (OSSL_STORE_INFO_get_type(info) == OSSL_STORE_INFO_PKEY) {
            pkey = OSSL_STORE_INFO_get1_PKEY(info);
            OSSL_STORE_INFO_free(info);
            break;
        }
        OSSL_STORE_INFO_free(info);
    }
    OSSL_STORE_close(store_ctx);

    if (pkey == NULL) {
        GG_LOGE("Failed to extract private key.");
        return GG_ERR_CONFIG;
    }

    if (SSL_CTX_use_PrivateKey(ssl_ctx, pkey) != 1) {
        GG_LOGE("Failed to use private key.");
        EVP_PKEY_free(pkey);
        return GG_ERR_CONFIG;
    }

    EVP_PKEY_free(pkey);
    return GG_ERR_OK;
}

static void cleanup_ssl_ctx(SSL_CTX **ctx) {
    if (*ctx != NULL) {
        SSL_CTX_free(*ctx);
    }
}

static void cleanup_bio_free_all(BIO **bio_ptr) {
    if ((bio_ptr != NULL) && (*bio_ptr != NULL)) {
        BIO_free_all(*bio_ptr);
    }
}

static GgError create_tls_context(
    const IotcoredArgs *args, SSL_CTX **ssl_ctx, bool enable_ktls
) {
    assert(ssl_ctx != NULL);
    SSL_CTX *new_ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (new_ssl_ctx == NULL) {
        GG_LOGE("Failed to create openssl context.");
        return GG_ERR_NOMEM;
    }
    GG_CLEANUP_ID(ctx_cleanup, cleanup_ssl_ctx, new_ssl_ctx);

    SSL_CTX_set_verify(new_ssl_ctx, SSL_VERIFY_PEER, NULL);

    if (SSL_CTX_load_verify_file(new_ssl_ctx, args->rootca) != 1) {
        GG_LOGE("Failed to load root CA.");
        return GG_ERR_CONFIG;
    }

    GgBuffer cert_buf = gg_buffer_from_null_term(args->cert);
    if (gg_buffer_has_prefix(cert_buf, GG_STR("pkcs11:"))) {
        GgError ret = load_cert_from_uri(new_ssl_ctx, args->cert);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        if (SSL_CTX_use_certificate_file(
                new_ssl_ctx, args->cert, SSL_FILETYPE_PEM
            )
            != 1) {
            GG_LOGE("Failed to load client certificate.");
            return GG_ERR_CONFIG;
        }
    }

    GgBuffer key_buf = gg_buffer_from_null_term(args->key);
    if (gg_buffer_has_prefix(key_buf, GG_STR("handle:"))
        || gg_buffer_has_prefix(key_buf, GG_STR("pkcs11:"))) {
        GgError ret = load_key_from_uri(new_ssl_ctx, args->key);
        if (ret != GG_ERR_OK) {
            return ret;
        }
    } else {
        if (SSL_CTX_use_PrivateKey_file(
                new_ssl_ctx, args->key, SSL_FILETYPE_PEM
            )
            != 1) {
            GG_LOGE("Failed to load client private key.");
            return GG_ERR_CONFIG;
        }
    }

    if (SSL_CTX_check_private_key(new_ssl_ctx) != 1) {
        GG_LOGE("Client certificate and private key do not match.");
        return GG_ERR_CONFIG;
    }

    if (enable_ktls) {
        try_enable_ktls(new_ssl_ctx);
    }

    ctx_cleanup = NULL;
    *ssl_ctx = new_ssl_ctx;
    return GG_ERR_OK;
}

static GgError do_handshake(char *host, BIO *bio) {
    SSL *ssl = NULL;
    BIO_get_ssl(bio, &ssl);

    assert(ssl != NULL);

    if (host != NULL) {
        if (SSL_set_tlsext_host_name(ssl, host) != 1) {
            GG_LOGE("Failed to configure SNI.");
            return GG_ERR_FATAL;
        }
    }

    if (SSL_do_handshake(ssl) != 1) {
        GG_LOGE("Failed TLS handshake.");
        return GG_ERR_FAILURE;
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        GG_LOGE("Failed TLS server certificate verification.");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static GgError iotcored_tls_connect_no_proxy(
    const IotcoredArgs *args, IotcoredTlsCtx **ctx
) {
    SSL_CTX *ssl_ctx = NULL;
    GgError ret = create_tls_context(args, &ssl_ctx, true);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP_ID(ctx_cleanup, cleanup_ssl_ctx, ssl_ctx);

    BIO *bio = BIO_new_ssl_connect(ssl_ctx);
    if (bio == NULL) {
        GG_LOGE("Failed to create openssl BIO.");
        return GG_ERR_FATAL;
    }
    GG_CLEANUP_ID(bio_cleanup, cleanup_bio_free_all, bio);

    if (BIO_set_conn_port(bio, "8883") != 1) {
        GG_LOGE("Failed to set port.");
        return GG_ERR_FATAL;
    }

    if (BIO_set_conn_hostname(bio, args->endpoint) != 1) {
        GG_LOGE("Failed to set hostname.");
        return GG_ERR_FATAL;
    }

    ret = do_handshake(args->endpoint, bio);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    SSL *ssl = NULL;
    BIO_get_ssl(bio, &ssl);
    check_ktls_status(ssl);

    ret = make_nonblocking(bio);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Since connection is established, cancel the cleanup.
    ctx_cleanup = NULL;
    bio_cleanup = NULL;

    conn = (IotcoredTlsCtx
    ) { .ssl_ctx = ssl_ctx, .bio = bio, .connected = true };
    *ctx = &conn;

    return GG_ERR_OK;
}

static GgError iotcored_proxy_connect_tunnel(
    const IotcoredArgs *args, GglUriInfo info, BIO *proxy_bio
) {
    char *proxy_user = NULL;
    char *proxy_password = NULL;
    // TODO: parse userinfo
    (void) info;
    // Tunnel to the IoT endpoint
    GG_LOGD("Connecting through the http proxy.");
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
        GG_LOGE("Failed http proxy connect.");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static GgError iotcored_tls_connect_https_proxy(
    const IotcoredArgs *args, IotcoredTlsCtx **ctx, GglUriInfo info
) {
    // Set up TLS before attempting a connection
    SSL_CTX *ssl_ctx = NULL;
    GgError ret = create_tls_context(args, &ssl_ctx, false);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP_ID(ctx_cleanup, cleanup_ssl_ctx, ssl_ctx);

    // Default fallback
    if (info.port.len == 0) {
        info.port = GG_STR("443");
    }

    // Connect to proxy via HTTPS
    BIO *mtls_proxy_bio = BIO_new_ssl_connect(ssl_ctx);
    if (mtls_proxy_bio == NULL) {
        GG_LOGE("Failed to create proxy socket.");
        return GG_ERR_FATAL;
    }
    GG_CLEANUP_ID(mtls_bio_cleanup, cleanup_bio_free_all, mtls_proxy_bio);

    if (BIO_set_conn_hostname(mtls_proxy_bio, info.host.data) != 1) {
        GG_LOGE("Failed to set proxy hostname.");
        return GG_ERR_FATAL;
    }
    if (BIO_set_conn_port(mtls_proxy_bio, info.port.data) != 1) {
        GG_LOGE("Failed to set proxy port.");
        return GG_ERR_FATAL;
    }
    GG_LOGD("Connecting to HTTPS proxy.");
    ret = do_handshake(NULL, mtls_proxy_bio);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to connect and handshake with proxy.");
        return ret;
    }

    // Connect the proxy server to IoT core and tunnel the connection
    ret = iotcored_proxy_connect_tunnel(args, info, mtls_proxy_bio);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to create tunnel.");
        return ret;
    }

    // This BIO is used to talk to IoT core.
    BIO *mqtt_bio = BIO_new_ssl(ssl_ctx, 1);
    if (mqtt_bio == NULL) {
        GG_LOGE("Failed to create openssl BIO.");
        return GG_ERR_FATAL;
    }
    GG_CLEANUP_ID(mqtt_bio_cleanup, cleanup_bio_free_all, mqtt_bio);

    // MQTT BIO uses the underlying HTTPS TLS BIO as its source and sync.
    BIO *mqtt_proxy_chain = BIO_push(mqtt_bio, mtls_proxy_bio);
    // Do handshake with IoT core over the established HTTPS TLS connection.
    ret = do_handshake(args->endpoint, mqtt_proxy_chain);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to connect and handshake with IoT core.");
        return ret;
    }

    // Since connection is established, cancel the cleanup.
    ctx_cleanup = NULL;
    mtls_bio_cleanup = NULL;
    mqtt_bio_cleanup = NULL;

    ret = make_nonblocking(mqtt_proxy_chain);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    conn = (IotcoredTlsCtx
    ) { .ssl_ctx = ssl_ctx, .bio = mqtt_proxy_chain, .connected = true };
    *ctx = &conn;

    return GG_ERR_OK;
}

static GgError iotcored_tls_connect_http_proxy(
    const IotcoredArgs *args, IotcoredTlsCtx **ctx, GglUriInfo info
) {
    // Set up TLS before attempting a connection
    SSL_CTX *ssl_ctx = NULL;
    GgError ret = create_tls_context(args, &ssl_ctx, true);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP_ID(ctx_cleanup, cleanup_ssl_ctx, ssl_ctx);

    BIO *mqtt_bio = BIO_new_ssl(ssl_ctx, 1);
    if (mqtt_bio == NULL) {
        GG_LOGE("Failed to create openssl BIO.");
        return GG_ERR_FATAL;
    }
    GG_CLEANUP_ID(mqtt_bio_cleanup, cleanup_bio_free_all, mqtt_bio);

    // default fallback
    if (info.port.len == 0) {
        info.port = GG_STR("80");
    }

    // open a plain-text socket to talk with proxy
    BIO *proxy_bio = BIO_new(BIO_s_connect());
    if (proxy_bio == NULL) {
        GG_LOGE("Failed to create proxy socket.");
        return GG_ERR_FATAL;
    }
    GG_CLEANUP_ID(proxy_bio_cleanup, cleanup_bio_free_all, proxy_bio);

    if (BIO_set_conn_hostname(proxy_bio, info.host.data) != 1) {
        GG_LOGE("Failed to set proxy hostname.");
        return GG_ERR_FATAL;
    }
    if (BIO_set_conn_port(proxy_bio, info.port.data) != 1) {
        GG_LOGE("Failed to set proxy port.");
        return GG_ERR_FATAL;
    }
    GG_LOGD("Connecting to HTTP proxy.");
    if (BIO_do_connect(proxy_bio) != 1) {
        GG_LOGE("Failed to connect to proxy.");
        return GG_ERR_FAILURE;
    }

    // Connect to the HTTP tunnel.
    ret = iotcored_proxy_connect_tunnel(args, info, proxy_bio);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    SSL *ssl = NULL;
    BIO_get_ssl(mqtt_bio, &ssl);
    check_ktls_status(ssl);

    // The proxy connection is the source and sink for all SSL bytes.
    BIO *mqtt_proxy_chain = BIO_push(mqtt_bio, proxy_bio);
    ret = do_handshake(args->endpoint, mqtt_proxy_chain);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Since connection is established, cancel the cleanup.
    ctx_cleanup = NULL;
    mqtt_bio_cleanup = NULL;
    proxy_bio_cleanup = NULL;

    ret = make_nonblocking(mqtt_proxy_chain);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    conn = (IotcoredTlsCtx
    ) { .ssl_ctx = ssl_ctx, .bio = mqtt_proxy_chain, .connected = true };
    *ctx = &conn;

    return GG_ERR_OK;
}

GgError iotcored_tls_connect(const IotcoredArgs *args, IotcoredTlsCtx **ctx) {
    GglUriInfo info = { 0 };
    GgError ret = proxy_get_info(args, &info);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (info.host.len > 0) {
        if (gg_buffer_eq(info.scheme, GG_STR("https"))) {
            ret = iotcored_tls_connect_https_proxy(args, ctx, info);
        } else if ((info.scheme.len == 0)
                   || gg_buffer_eq(info.scheme, GG_STR("http"))) {
            ret = iotcored_tls_connect_http_proxy(args, ctx, info);
        } else {
            GG_LOGE(
                "Unsupported scheme \"%.*s\".",
                (int) info.scheme.len,
                info.scheme.data
            );
        }
    } else {
        ret = iotcored_tls_connect_no_proxy(args, ctx);
    }

    if (ret != GG_ERR_OK) {
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return ret;
    }

    GG_LOGI("TLS connection established.");
    return GG_ERR_OK;
}

GgError iotcored_tls_read(IotcoredTlsCtx *ctx, GgBuffer *buf) {
    assert(ctx != NULL);
    assert(buf != NULL);

    if (!ctx->connected) {
        return GG_ERR_NOCONN;
    }

    SSL *ssl = NULL;
    BIO_get_ssl(ctx->bio, &ssl);

    size_t read_bytes = 0;
    int ret;
    int error_code;
    int err;
    {
        GG_MTX_SCOPE_GUARD(&ssl_mtx);
        ret = SSL_read_ex(ssl, buf->data, buf->len, &read_bytes);
        error_code = (ret != 1) ? SSL_get_error(ssl, ret) : 0;
        err = errno;
    }

    if (ret != 1) {
        switch (error_code) {
        case SSL_ERROR_WANT_READ:
        case SSL_ERROR_WANT_WRITE:
            // No data available on non-blocking socket.
            buf->len = 0;
            return GG_ERR_OK;
        case SSL_ERROR_SSL:
        case SSL_ERROR_SYSCALL:
            ERR_print_errors_cb(ssl_error_callback, NULL);
            errno = err;
            GG_LOGE("OpenSSL system error: %m.");
            ctx->connected = false;
            buf->len = 0;
            return GG_ERR_FATAL;
        case SSL_ERROR_ZERO_RETURN:
            GG_LOGE("Unexpected EOF.");
            buf->len = 0;
            return GG_ERR_FAILURE;
        default:
            ERR_print_errors_cb(ssl_error_callback, NULL);
            GG_LOGE("Unexpected SSL_read_ex error.");
            return GG_ERR_FAILURE;
        }
    }
    buf->len = read_bytes;
    return GG_ERR_OK;
}

GgError iotcored_tls_write(
    IotcoredTlsCtx *ctx, GgBuffer buf, bool *has_pending
) {
    assert(ctx != NULL);

    if (!ctx->connected) {
        return GG_ERR_NOCONN;
    }

    SSL *ssl = NULL;
    BIO_get_ssl(ctx->bio, &ssl);

    size_t written;
    int ret;
    int fd = -1;
    BIO_get_fd(ctx->bio, &fd);

    // Hold mutex for entire write — OpenSSL does not allow SSL_read
    // to interleave with a partial SSL_write.
    GG_MTX_SCOPE_GUARD(&ssl_mtx);
    do {
        ret = SSL_write_ex(ssl, buf.data, buf.len, &written);
        if (ret == 1) {
            *has_pending = SSL_has_pending(ssl);
            return GG_ERR_OK;
        }
        int error_code = SSL_get_error(ssl, ret);
        if ((error_code == SSL_ERROR_WANT_WRITE)
            || (error_code == SSL_ERROR_WANT_READ)) {
            // Wait for socket to become ready.
            // Must hold mutex — can't let SSL_read interleave
            // with a partial SSL_write.
            struct pollfd pfd = {
                .fd = fd,
                .events
                = (error_code == SSL_ERROR_WANT_WRITE) ? POLLOUT : POLLIN,
            };
            poll(&pfd, 1, -1);
            continue;
        }
        ERR_print_errors_cb(ssl_error_callback, NULL);
        switch (error_code) {
        case SSL_ERROR_SSL:
        case SSL_ERROR_SYSCALL:
            GG_LOGE("Connection unexpectedly closed.");
            ctx->connected = false;
            return GG_ERR_FATAL;
        default:
            GG_LOGW("Unexpected SSL_write_ex error.");
            return GG_ERR_FAILURE;
        }
    } while (true);
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
