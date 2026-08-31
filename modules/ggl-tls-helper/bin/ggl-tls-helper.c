// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <argp.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/buffer.h>
#include <ggl/cleanup.h>
#include <ggl/error.h>
#include <ggl/file.h>
#include <ggl/log.h>
#include <ggl/socket.h>
#include <netdb.h>
#include <openssl/bio.h>
#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/http.h>
#include <openssl/opensslv.h>
#include <openssl/prov_ssl.h>
#include <openssl/ssl.h>
#include <openssl/sslerr.h>
#include <openssl/types.h>
#include <openssl/x509.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

static char doc[] = "ggl-tls-helper -- AWS IoT Greengrass TLS plugin";

static struct argp_option opts[] = {
    { "endpoint", 'e', "address", 0, "Endpoint to connect to", 0 },
    { "port", 'p', "port", 0, "Port to use for TCP connection", 0 },
    { "root-ca", 'r', "path", 0, "Path to root CA certificate file", 0 },
    { "certificate", 'c', "path", 0, "Path to client certificate", 0 },
    { "private-key",
      'k',
      "path",
      0,
      "Path to private key for client certificate",
      0 },
    { "proxy", 'x', "url", 0, "Proxy URL (http:// or https://)", 0 },
    { 0 },
};

static char *arg_endpoint = NULL;
static char *arg_port = NULL;
static char *arg_root_ca = NULL;
static char *arg_certificate = NULL;
static char *arg_private_key = NULL;
static char *arg_proxy = NULL;

static error_t arg_parser(int key, char *arg, struct argp_state *state) {
    // NOLINTBEGIN(concurrency-mt-unsafe)
    switch (key) {
    case 'e':
        arg_endpoint = arg;
        break;
    case 'p':
        arg_port = arg;
        break;
    case 'k':
        arg_private_key = arg;
        break;
    case 'c':
        arg_certificate = arg;
        break;
    case 'r':
        arg_root_ca = arg;
        break;
    case 'x':
        arg_proxy = arg;
        break;
    case ARGP_KEY_END:
        if (arg_endpoint == NULL) {
            argp_error(state, "endpoint is required");
        }
        if (arg_port == NULL) {
            argp_error(state, "port is required");
        }
        if ((arg_certificate != NULL) && (arg_private_key == NULL)) {
            argp_error(state, "certificate requires private-key option");
        }
        if ((arg_private_key != NULL) && (arg_certificate == NULL)) {
            argp_error(state, "private-key requires certificate option");
        }
        break;
    default:
        return ARGP_ERR_UNKNOWN;
    }
    return 0;
    // NOLINTEND(concurrency-mt-unsafe)
}

static struct argp argp = { opts, arg_parser, 0, doc, 0, 0, 0 };

static GglError connect_with_timeout(
    int sockfd, const struct sockaddr *addr, socklen_t addrlen
) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1 || fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return GGL_ERR_FAILURE;
    }

    int sys_ret = connect(sockfd, addr, addrlen);
    if (sys_ret == 0) {
        // Connected immediately, restore blocking mode
        (void) fcntl(sockfd, F_SETFL, flags);
        return GGL_ERR_OK;
    }

    if (errno != EINPROGRESS) {
        GGL_LOGE("TCP socket connect failed: %m.");
        return GGL_ERR_FAILURE;
    }

    struct pollfd pfd = { .fd = sockfd, .events = POLLOUT };
    sys_ret = poll(&pfd, 1, 60000); // 60 second timeout
    if (sys_ret < 0) {
        GGL_LOGW("TCP socket poll error: %m.");
        return GGL_ERR_FAILURE;
    }
    if (sys_ret == 0) {
        GGL_LOGW("TCP socket connect timed out.");
        return GGL_ERR_TIMEOUT;
    }

    int error = 0;
    if (getsockopt(
            sockfd, SOL_SOCKET, SO_ERROR, &error, &(socklen_t) { sizeof(error) }
        )
        == -1) {
        GGL_LOGE("TCP socket getsockopt error: %m.");
        return GGL_ERR_FAILURE;
    }

    if (error != 0) {
        errno = error;
        GGL_LOGE("TCP socket connect error: %m.");
        return GGL_ERR_FAILURE;
    }

    // Restore blocking mode
    (void) fcntl(sockfd, F_SETFL, flags);
    return GGL_ERR_OK;
}

static GglError create_tcp_connection(
    const char *endpoint, const char *port, int *fd
) {
    char *endptr;
    long port_num = strtol(port, &endptr, 10);
    if ((*endptr != '\0') || (port_num < 1) || (port_num > 65535)) {
        GGL_LOGE("Invalid port: %s.", port);
        return GGL_ERR_INVALID;
    }

    GGL_LOGD("Connecting to %s:%s.", endpoint, port);

    struct addrinfo hints = { 0 };
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *result;
    int sys_ret = getaddrinfo(endpoint, port, &hints, &result);
    if (sys_ret != 0) {
        GGL_LOGE("getaddrinfo failed: %s.", gai_strerror(sys_ret));
        return GGL_ERR_FAILURE;
    }

    int sockfd = -1;
    for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
        sockfd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (sockfd == -1) {
            GGL_LOGW("Failed to create socket: %m.");
            continue;
        }

        GglError ret
            = connect_with_timeout(sockfd, rp->ai_addr, rp->ai_addrlen);
        if (ret != GGL_ERR_OK) {
            (void) close(sockfd);
            sockfd = -1;
            continue;
        }

        GGL_LOGD("Connected successfully.");
        break;
    }

    freeaddrinfo(result);

    if (sockfd == -1) {
        GGL_LOGE("Could not connect to %s:%s.", endpoint, port);
        return GGL_ERR_FAILURE;
    }

    *fd = sockfd;
    return GGL_ERR_OK;
}

static int openssl_err_cb(const char *str, size_t len, void *u) {
    (void) u;
    GGL_LOGE("openssl: %.*s", (int) len, str);
    return 0;
}

static void cleanup_ssl_ctx(SSL_CTX **ctx) {
    if (*ctx != NULL) {
        SSL_CTX_free(*ctx);
    }
}

static void cleanup_ssl(SSL **ssl) {
    if (*ssl != NULL) {
        SSL_free(*ssl);
    }
}

static GglError parse_proxy_url(
    const char *proxy_url, char **host, char **port
) {
    int use_ssl = 0;

    if (!OSSL_HTTP_parse_url(
            proxy_url, &use_ssl, NULL, host, port, NULL, NULL, NULL, NULL
        )) {
        GGL_LOGE("Failed to parse proxy URL.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_INVALID;
    }

    if (use_ssl) {
        GGL_LOGE("HTTPS proxies are not supported in this task.");
        return GGL_ERR_INVALID;
    }

    return GGL_ERR_OK;
}

static GglError http_proxy_connect(
    BIO *proxy_bio, const char *target_host, const char *target_port
) {
    GGL_LOGD("Sending HTTP CONNECT %s:%s to proxy.", target_host, target_port);

    int proxy_connect_ret = OSSL_HTTP_proxy_connect(
        proxy_bio,
        target_host,
        target_port,
        NULL, // proxy_user
        NULL, // proxy_password
        60, // timeout
        NULL, // bio_err
        NULL // prog
    );

    if (proxy_connect_ret != 1) {
        GGL_LOGE("Failed HTTP proxy CONNECT.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD("HTTP proxy CONNECT successful.");
    return GGL_ERR_OK;
}

static GglError tls_handshake(const char *endpoint, BIO *bio, SSL **ssl) {
    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (ssl_ctx == NULL) {
        GGL_LOGE("Failed to create openssl context.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_NOMEM;
    }
    GGL_CLEANUP(cleanup_ssl_ctx, ssl_ctx);

    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);
    SSL_CTX_set_mode(ssl_ctx, SSL_MODE_AUTO_RETRY);

    SSL_CTX_set_options(ssl_ctx, SSL_OP_ENABLE_KTLS);
    if (!(SSL_CTX_get_options(ssl_ctx) & SSL_OP_ENABLE_KTLS)) {
        GGL_LOGW("Failed to enable kTLS option on SSL context.");
    }

    SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_2_VERSION);
    // kTLS receive with TLS 1.3 is broken on OpenSSL < 3.2
    if (OPENSSL_VERSION_NUMBER < 0x30200000L) {
        SSL_CTX_set_max_proto_version(ssl_ctx, TLS1_2_VERSION);
    }

    if (arg_root_ca != NULL) {
        if (SSL_CTX_load_verify_file(ssl_ctx, arg_root_ca) != 1) {
            GGL_LOGE("Failed to load root CA.");
            ERR_print_errors_cb(openssl_err_cb, NULL);
            return GGL_ERR_CONFIG;
        }
    } else {
        if (SSL_CTX_set_default_verify_paths(ssl_ctx) != 1) {
            GGL_LOGE("Failed to load system certificate store.");
            ERR_print_errors_cb(openssl_err_cb, NULL);
            return GGL_ERR_FAILURE;
        }
    }

    if (arg_certificate != NULL && arg_private_key != NULL) {
        if (SSL_CTX_use_certificate_file(
                ssl_ctx, arg_certificate, SSL_FILETYPE_PEM
            )
            != 1) {
            GGL_LOGE("Failed to load client certificate.");
            ERR_print_errors_cb(openssl_err_cb, NULL);
            return GGL_ERR_CONFIG;
        }

        if (SSL_CTX_use_PrivateKey_file(
                ssl_ctx, arg_private_key, SSL_FILETYPE_PEM
            )
            != 1) {
            GGL_LOGE("Failed to load client private key.");
            ERR_print_errors_cb(openssl_err_cb, NULL);
            return GGL_ERR_CONFIG;
        }

        if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
            GGL_LOGE("Client certificate and private key do not match.");
            ERR_print_errors_cb(openssl_err_cb, NULL);
            return GGL_ERR_CONFIG;
        }
    }

    // Create SSL from context
    SSL *new_ssl = SSL_new(ssl_ctx);
    if (new_ssl == NULL) {
        GGL_LOGE("Failed to create SSL.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_NOMEM;
    }
    GGL_CLEANUP_ID(ssl_cleanup, cleanup_ssl, new_ssl);

    // SSL takes ownership of the BIO
    SSL_set_bio(new_ssl, bio, bio);

    if (SSL_set_tlsext_host_name(new_ssl, endpoint) != 1) {
        GGL_LOGE("Failed to configure SNI.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_FAILURE;
    }

    if (SSL_connect(new_ssl) != 1) {
        GGL_LOGE("Failed TLS handshake.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_FAILURE;
    }

    if (SSL_get_verify_result(new_ssl) != X509_V_OK) {
        GGL_LOGE("Failed TLS server certificate verification.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return GGL_ERR_FAILURE;
    }

    ssl_cleanup = NULL;
    *ssl = new_ssl;
    return GGL_ERR_OK;
}

static bool ktls_enabled(SSL *ssl) {
    bool tx_enabled = BIO_get_ktls_send(SSL_get_wbio(ssl)) > 0;
    bool rx_enabled = BIO_get_ktls_recv(SSL_get_rbio(ssl)) > 0;

    if (!tx_enabled) {
        GGL_LOGW("kTLS for send path is not active.");
    }
    if (!rx_enabled) {
        GGL_LOGW("kTLS for receive path is not active.");
    }

    return tx_enabled && rx_enabled;
}

static GglError send_socket_to_parent(int socket_fd) {
    struct stat st;
    if (fstat(3, &st) == -1 || !S_ISSOCK(st.st_mode)) {
        GGL_LOGE("File descriptor 3 is not a valid socket.");
        return GGL_ERR_FAILURE;
    }

    struct iovec iov = {
        .iov_base = (char *) "socket",
        .iov_len = 6,
    };

    char cmsg_buf[CMSG_SPACE(sizeof(int))];

    struct msghdr msg = {
        .msg_iov = &iov,
        .msg_iovlen = 1,
        .msg_control = cmsg_buf,
        .msg_controllen = sizeof(cmsg_buf),
    };

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &socket_fd, sizeof(int));

    ssize_t bytes_sent = sendmsg(3, &msg, 0);
    if (bytes_sent == -1) {
        GGL_LOGE("Failed to send socket to parent: %m.");
        return GGL_ERR_FAILURE;
    }

    if (bytes_sent != 6) {
        GGL_LOGE("Sent %zd bytes instead of 6.", bytes_sent);
        return GGL_ERR_FAILURE;
    }

    GGL_LOGD("Socket sent to parent successfully.");
    return GGL_ERR_OK;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int main(int argc, char *argv[]) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    argp_parse(&argp, argc, argv, 0, 0, NULL);

    GglError ret;
    int tcp_fd;
    bool using_proxy = false;

    if (arg_proxy != NULL) {
        char *parse_host;
        char *parse_port;
        ret = parse_proxy_url(arg_proxy, &parse_host, &parse_port);
        if (ret != GGL_ERR_OK) {
            return 1;
        }
        using_proxy = true;
        const char *port = (parse_port != NULL) ? parse_port : "80";
        ret = create_tcp_connection(parse_host, port, &tcp_fd);

        OPENSSL_free(parse_host);
        OPENSSL_free(parse_port);
    } else {
        ret = create_tcp_connection(arg_endpoint, arg_port, &tcp_fd);
    }
    if (ret != GGL_ERR_OK) {
        GGL_LOGE("Failed to create TCP connection.");
        return 1;
    }

    BIO *bio = BIO_new_socket(tcp_fd, BIO_NOCLOSE);
    if (bio == NULL) {
        GGL_LOGE("Failed to create openssl BIO.");
        ERR_print_errors_cb(openssl_err_cb, NULL);
        return 1;
    }

    if (using_proxy) {
        ret = http_proxy_connect(bio, arg_endpoint, arg_port);
        if (ret != GGL_ERR_OK) {
            return 1;
        }
    }

    SSL *ssl = NULL;
    ret = tls_handshake(arg_endpoint, bio, &ssl);
    if (ret != GGL_ERR_OK) {
        return 1;
    }
    GGL_CLEANUP(cleanup_ssl, ssl);

    GGL_LOGI("Connection established successfully.");

    bool using_ktls = ktls_enabled(ssl);

    int parent_fds[2];

    if (using_ktls) {
        GGL_LOGD("Using kernel TLS offload.");
        parent_fds[0] = tcp_fd;
        parent_fds[1] = -1;
    } else {
        int sys_ret = socketpair(AF_UNIX, SOCK_STREAM, 0, parent_fds);
        if (sys_ret == -1) {
            GGL_LOGE("Failed to create socketpair: %m.");
            return 1;
        }
    }

    ret = send_socket_to_parent(parent_fds[0]);
    if (ret != GGL_ERR_OK) {
        return 1;
    }

    if (using_ktls) {
        GGL_LOGD("Transferred kTLS socket; exiting.");
        return 0;
    }

    GGL_LOGD("Preparing to relay socket traffic.");

    // Close the parent's end of the socketpair
    (void) close(parent_fds[0]);
    int relay_fd = parent_fds[1];

    // Relay data between socketpair and TLS connection
    struct pollfd fds[2] = { { .fd = relay_fd, .events = POLLIN },
                             { .fd = tcp_fd, .events = POLLIN } };

    uint8_t buffer[4096];

    while (fds[0].fd >= 0 || fds[1].fd >= 0) {
        int poll_ret = poll(fds, 2, -1);
        if (poll_ret < 0) {
            GGL_LOGE("Poll error: %m.");
            return 1;
        }

        // Data from relay socket to TLS
        if (fds[0].revents & POLLIN) {
            GglBuffer buf = GGL_BUF(buffer);
            do {
                ret = ggl_file_read_partial(relay_fd, &buf);
            } while (ret == GGL_ERR_RETRY);

            if (ret == GGL_ERR_NODATA) {
                GGL_LOGD("Relay socket closed.");
                int shutdown_ret = SSL_shutdown(ssl);
                if (shutdown_ret < 0) {
                    GGL_LOGE("Openssl shutdown on network socket failed.");
                    ERR_print_errors_cb(openssl_err_cb, NULL);
                }
                (void) shutdown(tcp_fd, SHUT_WR);
                fds[0].fd = -1;
            } else if (ret != GGL_ERR_OK) {
                GGL_LOGE("Relay socket read error.");
                return 1;
            } else {
                size_t bytes_read = sizeof(buffer) - buf.len;
                int ssl_ret = SSL_write(ssl, buffer, (int) bytes_read);
                if (ssl_ret <= 0) {
                    GGL_LOGE("TLS write error.");
                    ERR_print_errors_cb(openssl_err_cb, NULL);
                    return 1;
                }
                if (ssl_ret != (int) bytes_read) {
                    GGL_LOGE(
                        "Unexpected OpenSSL partial write: %d of %zu bytes.",
                        ssl_ret,
                        bytes_read
                    );
                    ERR_print_errors_cb(openssl_err_cb, NULL);
                    return 1;
                }
            }
        }

        // Handle relay socket errors
        if (fds[0].revents & (POLLHUP | POLLERR)) {
            GGL_LOGD("Relay socket closed uncleanly.");
            fds[0].fd = -1;
        }

        // Data from TLS to relay socket
        if (fds[1].revents & POLLIN) {
            int bytes = SSL_read(ssl, buffer, sizeof(buffer));
            if (bytes <= 0) {
                int ssl_err = SSL_get_error(ssl, bytes);
                if (ssl_err == SSL_ERROR_ZERO_RETURN) {
                    GGL_LOGD("TLS connection EOF.");
                } else if (ssl_err == SSL_ERROR_SYSCALL) {
                    if (errno == EPIPE || errno == ECONNRESET) {
                        GGL_LOGD("TLS connection closed by peer.");
                    } else {
                        GGL_LOGE("OpenSSL read syscall error: %m.");
                        return 1;
                    }
                } else if (ssl_err == SSL_ERROR_SSL) {
                    unsigned long err = ERR_peek_error();
                    if (ERR_GET_REASON(err)
                        == SSL_R_UNEXPECTED_EOF_WHILE_READING) {
                        GGL_LOGD("TLS connection closed unexpectedly.");
                    } else {
                        GGL_LOGE("TLS read error.");
                        ERR_print_errors_cb(openssl_err_cb, NULL);
                        return 1;
                    }
                } else {
                    GGL_LOGE("TLS read error.");
                    ERR_print_errors_cb(openssl_err_cb, NULL);
                    return 1;
                }

                (void) shutdown(relay_fd, SHUT_WR);
                fds[1].fd = -1;
            } else {
                GglBuffer buf = { .data = buffer, .len = (size_t) bytes };
                ret = ggl_socket_write(relay_fd, buf);
                if (ret == GGL_ERR_NOCONN) {
                    GGL_LOGD("Relay socket closed by peer during write.");
                    fds[1].fd = -1;
                } else if (ret != GGL_ERR_OK) {
                    GGL_LOGE("Relay socket write error.");
                    return 1;
                }
            }
        }

        // Handle TLS socket errors
        if (fds[1].revents & (POLLHUP | POLLERR)) {
            GGL_LOGD("TLS socket closed uncleanly.");
            fds[1].fd = -1;
        }
    }

    close(tcp_fd);
    close(relay_fd);

    GGL_LOGD("TLS handling complete.");
}
