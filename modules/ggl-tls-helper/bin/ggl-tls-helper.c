// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <errno.h>
#include <fcntl.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <ggl/nucleus/init.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/prov_ssl.h>
#include <openssl/ssl.h>
#include <openssl/store.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define CONTROL_SOCKET_FD 3
#define TRANSPORT_FD 4
#define FORWARD_BUF_SIZE 16384

typedef struct {
    const char *hostname;
    const char *private_key;
    const char *certificate;
    const char *root_ca;
} TlsHelperArgs;

static char doc[] = "ggl-tls-helper -- TLS helper for Greengrass Lite";

static struct argp_option opts[] = {
    { "hostname", 'h', "HOST", 0, "Hostname for SNI", 0 },
    { "private-key", 'k', "PATH", 0, "Private key path or URI", 0 },
    { "certificate", 'c', "PATH", 0, "Certificate path or URI", 0 },
    { "root-ca", 'r', "PATH", 0, "Root CA path", 0 },
    { 0 },
};

// NOLINTNEXTLINE(readability-non-const-parameter)
static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    TlsHelperArgs *args = state->input;
    switch (key) {
    case 'h':
        args->hostname = arg;
        break;
    case 'k':
        args->private_key = arg;
        break;
    case 'c':
        args->certificate = arg;
        break;
    case 'r':
        args->root_ca = arg;
        break;
    case ARGP_KEY_END:
        if ((args->hostname == NULL) || (args->private_key == NULL)
            || (args->certificate == NULL) || (args->root_ca == NULL)) {
            // NOLINTNEXTLINE(concurrency-mt-unsafe)
            argp_usage(state);
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

static void unset_proxy_env(void) {
    static const char *vars[] = {
        "ALL_PROXY", "HTTP_PROXY", "HTTPS_PROXY", "NO_PROXY",
        "all_proxy", "http_proxy", "https_proxy", "no_proxy",
    };
    for (size_t i = 0; i < sizeof(vars) / sizeof(vars[0]); i++) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        unsetenv(vars[i]);
    }
}

static int ssl_error_callback(const char *str, size_t len, void *user) {
    (void) user;
    if (len > 0) {
        --len;
    }
    GG_LOGE("openssl: %.*s", (int) len, str);
    return 1;
}

static void cleanup_ssl_ctx(SSL_CTX **ctx) {
    if (*ctx != NULL) {
        SSL_CTX_free(*ctx);
    }
}

static void cleanup_ssl(SSL **s) {
    if (*s != NULL) {
        SSL_free(*s);
    }
}

static GgError load_certificate(SSL_CTX *ssl_ctx, const char *uri) {
    OSSL_STORE_CTX *store = OSSL_STORE_open(uri, NULL, NULL, NULL, NULL);
    if (store == NULL) {
        GG_LOGE("Failed to open certificate: %s", uri);
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_CONFIG;
    }
    X509 *cert = NULL;
    while (!OSSL_STORE_eof(store)) {
        OSSL_STORE_INFO *info = OSSL_STORE_load(store);
        if (info == NULL) {
            break;
        }
        if (OSSL_STORE_INFO_get_type(info) == OSSL_STORE_INFO_CERT) {
            cert = OSSL_STORE_INFO_get1_CERT(info);
            OSSL_STORE_INFO_free(info);
            break;
        }
        OSSL_STORE_INFO_free(info);
    }
    OSSL_STORE_close(store);
    if (cert == NULL) {
        GG_LOGE("No certificate found in: %s", uri);
        return GG_ERR_CONFIG;
    }
    int ok = SSL_CTX_use_certificate(ssl_ctx, cert);
    X509_free(cert);
    if (ok != 1) {
        GG_LOGE("Failed to use certificate.");
        return GG_ERR_CONFIG;
    }
    return GG_ERR_OK;
}

static GgError load_private_key(SSL_CTX *ssl_ctx, const char *uri) {
    OSSL_STORE_CTX *store = OSSL_STORE_open(uri, NULL, NULL, NULL, NULL);
    if (store == NULL) {
        GG_LOGE("Failed to open private key: %s", uri);
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_CONFIG;
    }
    EVP_PKEY *pkey = NULL;
    while (!OSSL_STORE_eof(store)) {
        OSSL_STORE_INFO *info = OSSL_STORE_load(store);
        if (info == NULL) {
            break;
        }
        if (OSSL_STORE_INFO_get_type(info) == OSSL_STORE_INFO_PKEY) {
            pkey = OSSL_STORE_INFO_get1_PKEY(info);
            OSSL_STORE_INFO_free(info);
            break;
        }
        OSSL_STORE_INFO_free(info);
    }
    OSSL_STORE_close(store);
    if (pkey == NULL) {
        GG_LOGE("No private key found in: %s", uri);
        return GG_ERR_CONFIG;
    }
    int ok = SSL_CTX_use_PrivateKey(ssl_ctx, pkey);
    EVP_PKEY_free(pkey);
    if (ok != 1) {
        GG_LOGE("Failed to use private key.");
        return GG_ERR_CONFIG;
    }
    return GG_ERR_OK;
}

static GgError tls_setup(
    const TlsHelperArgs *args,
    int transport_fd,
    SSL **out_ssl,
    SSL_CTX **out_ctx
) {
    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        GG_LOGE("Failed to create SSL context.");
        return GG_ERR_NOMEM;
    }
    GG_CLEANUP_ID(ctx_cleanup, cleanup_ssl_ctx, ssl_ctx);

    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
    SSL_CTX_set_options(ssl_ctx, SSL_OP_ENABLE_KTLS);

    if (SSL_CTX_load_verify_file(ssl_ctx, args->root_ca) != 1) {
        GG_LOGE("Failed to load root CA.");
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_CONFIG;
    }

    GgError err = load_certificate(ssl_ctx, args->certificate);
    if (err != GG_ERR_OK) {
        return err;
    }

    err = load_private_key(ssl_ctx, args->private_key);
    if (err != GG_ERR_OK) {
        return err;
    }

    if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
        GG_LOGE("Certificate and private key do not match.");
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_CONFIG;
    }

    SSL *ssl = SSL_new(ssl_ctx);
    if (ssl == NULL) {
        GG_LOGE("Failed to create SSL.");
        return GG_ERR_NOMEM;
    }
    GG_CLEANUP_ID(ssl_cleanup, cleanup_ssl, ssl);

    SSL_set_fd(ssl, transport_fd);

    if (SSL_set_tlsext_host_name(ssl, args->hostname) != 1) {
        GG_LOGE("Failed to set SNI.");
        return GG_ERR_FAILURE;
    }

    if (SSL_connect(ssl) != 1) {
        GG_LOGE("TLS handshake failed.");
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_FAILURE;
    }

    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        GG_LOGE("Server certificate verification failed.");
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_FAILURE;
    }

    GG_LOGI("TLS connection established.");

    ctx_cleanup = NULL;
    ssl_cleanup = NULL;
    *out_ssl = ssl;
    *out_ctx = ssl_ctx;
    return GG_ERR_OK;
}

static GgError send_fd_on_control_socket(int fd_to_send) {
    char payload[] = "socket";
    struct iovec iov = { .iov_base = payload, .iov_len = sizeof(payload) - 1 };

    union {
        char buf[CMSG_SPACE(sizeof(int))];
        struct cmsghdr align;
    } cmsg_buf;

    memset(&cmsg_buf, 0, sizeof(cmsg_buf));

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsg_buf.buf,
        .msg_controllen = sizeof(cmsg_buf.buf),
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd_to_send, sizeof(int));

    ssize_t ret = sendmsg(CONTROL_SOCKET_FD, &msg, 0);
    if (ret < 0) {
        GG_LOGE("sendmsg on control socket failed: %m.");
        return GG_ERR_FAILURE;
    }

    return GG_ERR_OK;
}

static GgError make_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if ((flags == -1) || (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)) {
        GG_LOGE("Failed to set fd non-blocking: %m.");
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError ssl_write_all(SSL *ssl, int transport_fd, GgBuffer data) {
    size_t written = 0;
    for (;;) {
        int ssl_ret = SSL_write_ex(ssl, data.data, data.len, &written);
        if (ssl_ret == 1) {
            return GG_ERR_OK;
        }
        int err = SSL_get_error(ssl, ssl_ret);
        if ((err == SSL_ERROR_WANT_READ) || (err == SSL_ERROR_WANT_WRITE)) {
            struct pollfd pfd = {
                .fd = transport_fd,
                .events
                = (short) ((err == SSL_ERROR_WANT_WRITE) ? POLLOUT : POLLIN),
            };
            poll(&pfd, 1, 10000);
            continue;
        }
        GG_LOGE("SSL write failed.");
        ERR_print_errors_cb(ssl_error_callback, NULL);
        return GG_ERR_FAILURE;
    }
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GgError forward_loop(SSL *ssl, int pair_fd, int transport_fd) {
    GgError ret = make_nonblocking(pair_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    ret = make_nonblocking(transport_fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    static uint8_t buf[FORWARD_BUF_SIZE];
    bool eof_from_tls = false;
    bool eof_from_parent = false;

    while (!eof_from_tls || !eof_from_parent) {
        // SSL_has_pending: skip poll if OpenSSL has buffered data
        if (!SSL_has_pending(ssl)) {
            struct pollfd fds[2] = {
                { .fd = transport_fd,
                  .events = (short) (!eof_from_tls ? POLLIN : 0) },
                { .fd = pair_fd,
                  .events = (short) (!eof_from_parent ? POLLIN : 0) },
            };

            int pret;
            do {
                pret = poll(fds, 2, 10000);
            } while ((pret < 0) && (errno == EINTR));
            if (pret < 0) {
                GG_LOGE("poll failed: %m.");
                return GG_ERR_FAILURE;
            }

            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (!eof_from_tls) {
                    eof_from_tls = true;
                    shutdown(pair_fd, SHUT_WR);
                }
            }
            if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
                if (!eof_from_parent) {
                    eof_from_parent = true;
                    shutdown(transport_fd, SHUT_WR);
                }
            }
        }

        // TLS → parent
        if (!eof_from_tls) {
            size_t read_bytes = 0;
            int ssl_ret = SSL_read_ex(ssl, buf, sizeof(buf), &read_bytes);
            if (ssl_ret == 1) {
                ret = gg_file_write(
                    pair_fd, (GgBuffer) { .data = buf, .len = read_bytes }
                );
                if (ret != GG_ERR_OK) {
                    GG_LOGE("write to socketpair failed: %m.");
                    return ret;
                }
            } else {
                int err = SSL_get_error(ssl, ssl_ret);
                if (err == SSL_ERROR_ZERO_RETURN) {
                    eof_from_tls = true;
                    shutdown(pair_fd, SHUT_WR);
                } else if ((err != SSL_ERROR_WANT_READ)
                           && (err != SSL_ERROR_WANT_WRITE)) {
                    ERR_print_errors_cb(ssl_error_callback, NULL);
                    GG_LOGE("SSL_read_ex error.");
                    return GG_ERR_FAILURE;
                }
            }
        }

        // Parent → TLS
        if (!eof_from_parent) {
            ssize_t n = read(pair_fd, buf, sizeof(buf));
            if (n > 0) {
                ret = ssl_write_all(
                    ssl,
                    transport_fd,
                    (GgBuffer) { .data = buf, .len = (size_t) n }
                );
                if (ret != GG_ERR_OK) {
                    return ret;
                }
            } else if (n == 0) {
                eof_from_parent = true;
                SSL_shutdown(ssl);
            } else if ((errno != EAGAIN) && (errno != EWOULDBLOCK)
                       && (errno != EINTR)) {
                GG_LOGE("read from socketpair failed: %m.");
                return GG_ERR_FAILURE;
            }
        }
    }

    return GG_ERR_OK;
}

int main(int argc, char **argv) {
    unset_proxy_env();

    static TlsHelperArgs args = { 0 };
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, &args);

    ggl_nucleus_init();

    SSL *ssl = NULL;
    SSL_CTX *ssl_ctx = NULL;
    GgError ret = tls_setup(&args, TRANSPORT_FD, &ssl, &ssl_ctx);
    if (ret != GG_ERR_OK) {
        return 1;
    }
    GG_CLEANUP(cleanup_ssl, ssl);
    GG_CLEANUP(cleanup_ssl_ctx, ssl_ctx);

    // If kTLS is fully active, send the raw TCP fd directly
    BIO *wbio = SSL_get_wbio(ssl);
    BIO *rbio = SSL_get_rbio(ssl);
    int ktls_tx = BIO_get_ktls_send(wbio);
    int ktls_rx = BIO_get_ktls_recv(rbio);
    if ((ktls_tx >= 1) && (ktls_rx >= 1)) {
        if (SSL_has_pending(ssl)) {
            // Should not happen for MQTT or HTTP since the client speaks
            // first, but guard against data loss if it ever does.
            GG_LOGE("kTLS active but OpenSSL has buffered data; aborting.");
            return 1;
        }
        GG_LOGI("kTLS active on both TX and RX; sending raw TCP fd.");
        ret = send_fd_on_control_socket(TRANSPORT_FD);
        SSL_set_quiet_shutdown(ssl, 1);
        return ret != GG_ERR_OK;
    }

    GG_LOGD(
        "kTLS not fully active (tx=%d rx=%d); using forwarding loop.",
        ktls_tx,
        ktls_rx
    );

    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) < 0) {
        GG_LOGE("socketpair failed: %m.");
        return 1;
    }
    GG_CLEANUP(cleanup_close, sv[0]);

    ret = send_fd_on_control_socket(sv[1]);
    (void) gg_close(sv[1]);
    if (ret != GG_ERR_OK) {
        return 1;
    }

    ret = forward_loop(ssl, sv[0], TRANSPORT_FD);
    return ret != GG_ERR_OK;
}
