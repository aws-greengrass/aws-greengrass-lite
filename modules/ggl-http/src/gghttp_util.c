// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "gghttp_util.h"
#include <assert.h>
#include <curl/curl.h>
#include <errno.h>
#include <gg/arena.h>
#include <gg/backoff.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/http.h>
#include <limits.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/store.h>
#include <openssl/types.h>
#include <pthread.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#define MAX_HEADER_LENGTH 8192

__attribute__((constructor)) static void init_curl(void) {
    // TODO: set up a heap4 and init curl instead with curl_global_init_mem()
    CURLcode e = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (e != CURLE_OK) {
        GG_LOGE(
            "Failed to init curl with CURLcode %d (reason: \"%s\").",
            e,
            curl_easy_strerror(e)
        );
        _Exit(1);
    }
}

static GgError translate_curl_code(CURLcode code) {
    switch (code) {
    case CURLE_OK:
        return GG_ERR_OK;
    case CURLE_AGAIN:
        return GG_ERR_RETRY;
    case CURLE_URL_MALFORMAT:
        return GG_ERR_PARSE;
    case CURLE_ABORTED_BY_CALLBACK:
    case CURLE_WRITE_ERROR:
        return GG_ERR_FAILURE;
    default:
        return GG_ERR_REMOTE;
    }
}

static bool can_retry(CURLcode code, CurlData *data) {
    switch (code) {
    // If OK, then inspect HTTP status code.
    case CURLE_OK:
        break;

    case CURLE_OPERATION_TIMEDOUT:
    case CURLE_COULDNT_CONNECT:
    case CURLE_SSL_CONNECT_ERROR:
    case CURLE_GOT_NOTHING:
    case CURLE_SEND_ERROR:
    case CURLE_RECV_ERROR:
    case CURLE_PARTIAL_FILE:
    case CURLE_AGAIN:
        return true;

    default:
        return false;
    }

    long http_status_code = 0;
    curl_easy_getinfo(data->curl, CURLINFO_HTTP_CODE, &http_status_code);

    switch (http_status_code) {
    case 400: // Generic client error
    case 408: // Request timeout
              // TODO: 429 can contain a retry-after header.
              // This should be used as the backoff.
              // Also add a upper limit to retry-after
    case 429: // Too many requests
    case 500: // Generic server error
    case 502: // Bad gateway
    case 503: // Service unavailable
    case 504: // Gateway Timeout
    case 509: // Server bandwidth exceeded
        return true;
    default:
        return false;
    }
}

typedef struct CurlRequestRetryCtx {
    CurlData *curl_data;

    // reset response_data for next attempt
    GgError (*retry_fn)(void *);
    void *response_data;

    // Needed to propagate errors when retrying is impossible.
    GgError err;
} CurlRequestRetryCtx;

static GgError clear_buffer(void *response_data) {
    GgByteVec *vector = (GgByteVec *) response_data;
    vector->buf.len = 0;
    return GG_ERR_OK;
}

static GgError truncate_file(void *response_data) {
    int fd = *(int *) response_data;

    int ret;
    do {
        ret = ftruncate(fd, 0);
    } while ((ret == -1) && (errno == EINTR));

    if (ret == -1) {
        GG_LOGE("Failed to truncate fd for write (errno=%d).", errno);
        return GG_ERR_FAILURE;
    }
    return GG_ERR_OK;
}

static GgError curl_request_retry_wrapper(void *ctx) {
    CurlRequestRetryCtx *retry_ctx = (CurlRequestRetryCtx *) ctx;
    CurlData *curl_data = retry_ctx->curl_data;

    CURLcode curl_error = curl_easy_perform(curl_data->curl);
    if (can_retry(curl_error, curl_data)) {
        GgError err = retry_ctx->retry_fn(retry_ctx->response_data);
        if (err != GG_ERR_OK) {
            retry_ctx->err = err;
            return GG_ERR_OK;
        }
        return GG_ERR_FAILURE;
    }
    if (curl_error != CURLE_OK) {
        GG_LOGE(
            "Curl request failed due to error: %s",
            curl_easy_strerror(curl_error)
        );
        retry_ctx->err = translate_curl_code(curl_error);
        return GG_ERR_OK;
    }
    long http_status_code = 0;
    curl_error = curl_easy_getinfo(
        curl_data->curl, CURLINFO_HTTP_CODE, &http_status_code
    );
    if (curl_error != CURLE_OK) {
        retry_ctx->err = GG_ERR_FAILURE;
        return GG_ERR_OK;
    }

    if ((http_status_code >= 200) && (http_status_code < 300)) {
        retry_ctx->err = GG_ERR_OK;
        return GG_ERR_OK;
    }

    if ((http_status_code >= 500) && (http_status_code < 600)) {
        retry_ctx->err = GG_ERR_REMOTE;
    } else {
        retry_ctx->err = GG_ERR_FAILURE;
    }
    GG_LOGE(
        "Curl request failed due to HTTP status code %ld.", http_status_code
    );
    return GG_ERR_OK;
}

static GgError do_curl_request(
    CurlData *curl_data, GgByteVec *response_buffer
) {
    CurlRequestRetryCtx ctx = { .curl_data = curl_data,
                                .response_data = (void *) response_buffer,
                                .retry_fn = clear_buffer,
                                .err = GG_ERR_OK };
    GgError ret
        = gg_backoff(1000, 64000, 7, curl_request_retry_wrapper, (void *) &ctx);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    if (ctx.err != GG_ERR_OK) {
        return ctx.err;
    }
    return GG_ERR_OK;
}

static GgError do_curl_request_fd(CurlData *curl_data, int fd) {
    CurlRequestRetryCtx ctx = { .curl_data = curl_data,
                                .response_data = (void *) &fd,
                                .retry_fn = truncate_file,
                                .err = GG_ERR_OK };
    GgError ret
        = gg_backoff(1000, 64000, 7, curl_request_retry_wrapper, (void *) &ctx);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Curl request failed; retries exhausted.");
        return ret;
    }
    if (ctx.err != GG_ERR_OK) {
        return ctx.err;
    }
    return GG_ERR_OK;
}

/// @brief Callback function to write the HTTP response data to a buffer.
///
/// This function is used as a callback by CURL to handle the response data
/// received from an HTTP request. It reallocates memory for the output buffer
/// and copies the response data into the buffer.This function will be called
/// multiple times when a new data is fetched via libcurl.
///
/// @param[in] response_data A pointer to the response data received from CURL.
/// @param[in] size The size of each element in the response data.
/// @param[in] nmemb The number of elements in the response data.
/// @param[in] output_vector_void A pointer to a vector which will be appended.
///
/// @return The number of bytes written to the output buffer.
static size_t write_response_to_buffer(
    void *response_data, size_t size, size_t nmemb, void *output_vector_void
) {
    if (response_data == NULL) {
        return 0;
    }
    size_t size_of_response_data = size * nmemb;
    GgBuffer response_buffer
        = (GgBuffer) { .data = response_data, .len = size_of_response_data };
    assert(output_vector_void != NULL);
    GgByteVec *output_vector = output_vector_void;
    GgError ret = gg_byte_vec_append(output_vector, response_buffer);
    if (ret != GG_ERR_OK) {
        size_t remaining_capacity
            = gg_byte_vec_remaining_capacity(*output_vector).len;
        GG_LOGE(
            "Not enough space to hold full body. Est. remaining bytes: %zu. Buffer remaining capacity: %zu",
            size_of_response_data,
            remaining_capacity
        );
        return 0;
    }

    return size_of_response_data;
}

/// @brief Callback function to write the HTTP response data to a file
/// descriptor.
///
/// This function is used as a callback by CURL to handle the response data
/// received from an HTTP request. It write bytes received into the file
/// descriptor.
///
/// @param[in] response_data A pointer to the response data received from CURL.
/// @param[in] size The size of each element in the response data.
/// @param[in] nmemb The number of elements in the response data.
/// @param[in] fd_void A pointer to a file descriptor
///
/// @return The number of bytes written.
static size_t write_response_to_fd(
    void *response_data, size_t size, size_t nmemb, void *fd_void
) {
    if (response_data == NULL) {
        return 0;
    }
    size_t size_of_response_data = size * nmemb;
    GgBuffer response_buffer
        = (GgBuffer) { .data = response_data, .len = size_of_response_data };
    assert(fd_void != NULL);
    int *fd = (int *) fd_void;
    GgError err = gg_file_write(*fd, response_buffer);
    if (err != GG_ERR_OK) {
        return 0;
    }
    return size_of_response_data;
}

/// @brief Set HTTPS proxy configuration for curl requests if enabled or setup
/// by the config.
///
/// Depending on whether HTTPS proxy is enabled, this function will attempt to
/// add the root CA trust store to the curl's configuration. On success, it will
/// try to optionally add the key and cert for mTLS HTTPS proxy if configured.
///
/// @param[in] curl_data A pointer to the curl data which is to be updated with
/// the proxy configuration.
/// @return GG_ERR_OK on success, error code otherwise.
static GgError set_curl_proxy_config(CurlData *curl_data) {
    assert(curl_data != NULL);

    if (curl_data == NULL) {
        GG_LOGE("Pointer to curl data cannot be NULL");
        return GG_ERR_FATAL;
    }

    uint8_t proxy_uri_mem[PATH_MAX] = { 0 };
    GgArena alloc_proxy = gg_arena_init(
        gg_buffer_substr(GG_BUF(proxy_uri_mem), 0, sizeof(proxy_uri_mem) - 1)
    );
    GgBuffer proxy_uri;
    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("networkProxy"),
            GG_STR("proxy"),
            GG_STR("url")
        ),
        &alloc_proxy,
        &proxy_uri
    );

    if (ret == GG_ERR_OK) {
        proxy_uri_mem[proxy_uri.len] = '\0';

        if ((proxy_uri.len > 5)
            && (strncmp((const char *) proxy_uri_mem, "https", 5) == 0)) {
            uint8_t ca_mem[PATH_MAX] = { 0 };
            GgArena alloc_ca = gg_arena_init(
                gg_buffer_substr(GG_BUF(ca_mem), 0, sizeof(ca_mem) - 1)
            );
            GgBuffer ca;

            ret = ggl_gg_config_read_str(
                GG_BUF_LIST(GG_STR("system"), GG_STR("rootCaPath")),
                &alloc_ca,
                &ca
            );
            if (ret != GG_ERR_OK) {
                GG_LOGE("No root CA provided for https proxy.");
                return GG_ERR_FAILURE;
            }

            ca_mem[ca.len] = '\0';
            GG_LOGI("Proxy CA path %s", ca_mem);
            CURLcode curl_error = curl_easy_setopt(
                curl_data->curl, CURLOPT_PROXY_CAINFO, ca_mem
            );
            if (curl_error != CURLE_OK) {
                return translate_curl_code(curl_error);
            }

            uint8_t cert_mem[PATH_MAX] = { 0 };
            GgArena alloc_cert = gg_arena_init(
                gg_buffer_substr(GG_BUF(cert_mem), 0, sizeof(cert_mem) - 1)
            );
            GgBuffer cert;

            ret = ggl_gg_config_read_str(
                GG_BUF_LIST(
                    GG_STR("services"),
                    GG_STR("aws.greengrass.NucleusLite"),
                    GG_STR("configuration"),
                    GG_STR("networkProxy"),
                    GG_STR("proxy"),
                    GG_STR("proxyCertPath")
                ),
                &alloc_cert,
                &cert
            );
            if (ret != GG_ERR_OK) {
                GG_LOGD(
                    "No certificate provided to be used with https proxy. Not setting cert/key in curl config."
                );

                // Return here with success.
                return GG_ERR_OK;
            }
            cert_mem[cert.len] = '\0';

            uint8_t key_mem[PATH_MAX] = { 0 };
            GgArena alloc_key = gg_arena_init(
                gg_buffer_substr(GG_BUF(key_mem), 0, sizeof(key_mem) - 1)
            );
            GgBuffer key;

            ret = ggl_gg_config_read_str(
                GG_BUF_LIST(
                    GG_STR("services"),
                    GG_STR("aws.greengrass.NucleusLite"),
                    GG_STR("configuration"),
                    GG_STR("networkProxy"),
                    GG_STR("proxy"),
                    GG_STR("proxyKeyPath")
                ),
                &alloc_key,
                &key
            );
            if (ret != GG_ERR_OK) {
                GG_LOGD(
                    "No key provided to be used with https proxy. Not setting cert/key in curl config."
                );

                // Return here with success.
                return GG_ERR_OK;
            }
            key_mem[key.len] = '\0';

            // Once we have paths for key and cert, try to add them to the curl
            // config.
            GG_LOGI("Proxy cert path %s", cert_mem);
            curl_error = curl_easy_setopt(
                curl_data->curl, CURLOPT_PROXY_SSLCERT, cert_mem
            );
            if (curl_error != CURLE_OK) {
                return translate_curl_code(curl_error);
            }

            GG_LOGW("Proxy key path %s", key_mem);
            curl_error = curl_easy_setopt(
                curl_data->curl, CURLOPT_PROXY_SSLKEY, key_mem
            );
            if (curl_error != CURLE_OK) {
                return translate_curl_code(curl_error);
            }
        }
    }

    return GG_ERR_OK;
}

void gghttplib_destroy_curl(CurlData *curl_data) {
    assert(curl_data != NULL);
    if (curl_data->headers_list != NULL) {
        curl_slist_free_all(curl_data->headers_list);
        curl_data->headers_list = NULL;
    }
    curl_easy_cleanup(curl_data->curl);
}

GgError gghttplib_init_curl(CurlData *curl_data, const char *url) {
    curl_data->headers_list = NULL;
    curl_data->curl = curl_easy_init();

    if (curl_data->curl == NULL) {
        GG_LOGE("Cannot create instance of curl for the url=%s", url);
        return GG_ERR_FAILURE;
    }

    CURLcode err = curl_easy_setopt(curl_data->curl, CURLOPT_URL, url);

    return translate_curl_code(err);
}

GgError gghttplib_add_header(
    CurlData *curl_data, GgBuffer header_key, GgBuffer header_value
) {
    assert(curl_data != NULL);
    static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    GG_MTX_SCOPE_GUARD(&mtx);
    static char header[MAX_HEADER_LENGTH];
    GgByteVec header_vec = GG_BYTE_VEC(header);
    GgError err = GG_ERR_OK;
    // x-header-key: header-value
    gg_byte_vec_chain_append(&err, &header_vec, header_key);
    gg_byte_vec_chain_push(&err, &header_vec, ':');
    gg_byte_vec_chain_push(&err, &header_vec, ' ');
    gg_byte_vec_chain_append(&err, &header_vec, header_value);
    gg_byte_vec_chain_push(&err, &header_vec, '\0');
    if (err != GG_ERR_OK) {
        return err;
    }
    struct curl_slist *new_head
        = curl_slist_append(curl_data->headers_list, header);
    if (new_head == NULL) {
        return GG_ERR_FAILURE;
    }
    curl_data->headers_list = new_head;
    return GG_ERR_OK;
}

static CURLcode ssl_ctx_callback(CURL *curl, void *ssl_ctx, void *ptr) {
    (void) curl;
    TpmCallbackData *data = (TpmCallbackData *) ptr;
    SSL_CTX *ctx = (SSL_CTX *) ssl_ctx;

    if (SSL_CTX_use_certificate_file(ctx, data->cert_path, SSL_FILETYPE_PEM)
        != 1) {
        return CURLE_SSL_CERTPROBLEM;
    }

    OSSL_STORE_CTX *store_ctx
        = OSSL_STORE_open(data->key_path, NULL, NULL, NULL, NULL);
    if (store_ctx == NULL) {
        return CURLE_SSL_CERTPROBLEM;
    }

    OSSL_STORE_INFO *info = OSSL_STORE_load(store_ctx);
    if (info == NULL) {
        OSSL_STORE_close(store_ctx);
        return CURLE_SSL_CERTPROBLEM;
    }

    EVP_PKEY *pkey = OSSL_STORE_INFO_get1_PKEY(info);
    OSSL_STORE_INFO_free(info);
    OSSL_STORE_close(store_ctx);

    if (pkey == NULL) {
        return CURLE_SSL_CERTPROBLEM;
    }

    if (SSL_CTX_use_PrivateKey(ctx, pkey) != 1) {
        EVP_PKEY_free(pkey);
        return CURLE_SSL_CERTPROBLEM;
    }

    if (SSL_CTX_check_private_key(ctx) != 1) {
        EVP_PKEY_free(pkey);
        return CURLE_SSL_CERTPROBLEM;
    }

    EVP_PKEY_free(pkey);
    return CURLE_OK;
}

GgError gghttplib_add_certificate_data(
    CurlData *curl_data, CertificateDetails request_data
) {
    assert(curl_data != NULL);

    if (strncmp(request_data.gghttplib_p_key_path, "handle:", 7) == 0) {
        curl_data->tpm_data.key_path = request_data.gghttplib_p_key_path;
        curl_data->tpm_data.cert_path = request_data.gghttplib_cert_path;

        CURLcode err = curl_easy_setopt(
            curl_data->curl, CURLOPT_SSL_CTX_FUNCTION, ssl_ctx_callback
        );
        if (err != CURLE_OK) {
            GG_LOGE(
                "Failed to set SSL context callback: %s",
                curl_easy_strerror(err)
            );
            return translate_curl_code(err);
        }

        err = curl_easy_setopt(
            curl_data->curl, CURLOPT_SSL_CTX_DATA, (void *) &curl_data->tpm_data
        );
        if (err != CURLE_OK) {
            GG_LOGE(
                "Failed to set SSL context data: %s", curl_easy_strerror(err)
            );
            return translate_curl_code(err);
        }
    } else {
        CURLcode err = curl_easy_setopt(
            curl_data->curl, CURLOPT_SSLCERT, request_data.gghttplib_cert_path
        );
        if (err != CURLE_OK) {
            GG_LOGE(
                "Failed to set CURLOPT_SSLCERT: %s", curl_easy_strerror(err)
            );
            return translate_curl_code(err);
        }

        err = curl_easy_setopt(
            curl_data->curl, CURLOPT_SSLKEY, request_data.gghttplib_p_key_path
        );
        if (err != CURLE_OK) {
            GG_LOGE(
                "Failed to set CURLOPT_SSLKEY: %s", curl_easy_strerror(err)
            );
            return translate_curl_code(err);
        }
    }

    CURLcode err = curl_easy_setopt(
        curl_data->curl, CURLOPT_CAINFO, request_data.gghttplib_root_ca_path
    );
    if (err != CURLE_OK) {
        GG_LOGE("Failed to set CURLOPT_CAINFO: %s", curl_easy_strerror(err));
    }
    return translate_curl_code(err);
}

GgError gghttplib_add_post_body(CurlData *curl_data, const char *body) {
    assert(curl_data != NULL);
    CURLcode err = curl_easy_setopt(curl_data->curl, CURLOPT_POSTFIELDS, body);
    return translate_curl_code(err);
}

GgError gghttplib_process_request(
    CurlData *curl_data, GgBuffer *response_buffer
) {
    assert(curl_data != NULL);
    GgByteVec response_vector = (response_buffer != NULL)
        ? gg_byte_vec_init(*response_buffer)
        : (GgByteVec) { 0 };

    CURLcode curl_error = curl_easy_setopt(
        curl_data->curl, CURLOPT_HTTPHEADER, curl_data->headers_list
    );
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }

    GgError ret = set_curl_proxy_config(curl_data);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (response_buffer != NULL) {
        curl_error = curl_easy_setopt(
            curl_data->curl, CURLOPT_WRITEFUNCTION, write_response_to_buffer
        );
        if (curl_error != CURLE_OK) {
            return translate_curl_code(curl_error);
        }
        curl_error =
            // coverity[bad_sizeof]
            curl_easy_setopt(
                curl_data->curl, CURLOPT_WRITEDATA, (void *) &response_vector
            );
        if (curl_error != CURLE_OK) {
            return translate_curl_code(curl_error);
        }
    }

    ret = do_curl_request(curl_data, &response_vector);
    if ((response_buffer != NULL) && (ret == GG_ERR_OK)) {
        response_buffer->len = response_vector.buf.len;
    }
    return ret;
}

GgError gghttplib_process_request_with_fd(CurlData *curl_data, int fd) {
    CURLcode curl_error = curl_easy_setopt(
        curl_data->curl, CURLOPT_HTTPHEADER, curl_data->headers_list
    );
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }

    GgError ret = set_curl_proxy_config(curl_data);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    curl_error = curl_easy_setopt(
        curl_data->curl, CURLOPT_WRITEFUNCTION, write_response_to_fd
    );
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }

    curl_error =
        // coverity[bad_sizeof]
        curl_easy_setopt(curl_data->curl, CURLOPT_WRITEDATA, (void *) &fd);
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }
    curl_error = curl_easy_setopt(curl_data->curl, CURLOPT_FAILONERROR, 1L);
    if (curl_error != CURLE_OK) {
        return translate_curl_code(curl_error);
    }

    return do_curl_request_fd(curl_data, fd);
}
