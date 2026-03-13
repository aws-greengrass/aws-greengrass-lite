// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "proxy.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <gg/types.h>
#include <gg/vector.h>
#include <ggl/core_bus/gg_config.h>
#include <ggl/uri.h>
#include <inttypes.h>
#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/// Read proxy_url from env vars, then gg config store.
static GgError get_proxy_url(GgArena *alloc, GgBuffer *out, bool *found) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *val = getenv("https_proxy");
    if (val == NULL || val[0] == '\0') {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        val = getenv("HTTPS_PROXY");
    }
    if (val != NULL && val[0] != '\0') {
        *out = gg_buffer_from_null_term((char *) val);
        *found = true;
        return GG_ERR_OK;
    }

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("networkProxy"),
            GG_STR("proxy"),
            GG_STR("url")
        ),
        alloc,
        out
    );
    if (ret == GG_ERR_NOENTRY) {
        *found = false;
        return GG_ERR_OK;
    }
    if (ret != GG_ERR_OK) {
        return ret;
    }

    *found = true;
    return GG_ERR_OK;
}

/// Read no_proxy from env vars, then gg config store.
static GgError get_no_proxy(GgArena *alloc, GgBuffer *out, bool *found) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char *val = getenv("no_proxy");
    if (val == NULL || val[0] == '\0') {
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        val = getenv("NO_PROXY");
    }
    if (val != NULL && val[0] != '\0') {
        *out = gg_buffer_from_null_term((char *) val);
        *found = true;
        return GG_ERR_OK;
    }

    GgError ret = ggl_gg_config_read_str(
        GG_BUF_LIST(
            GG_STR("services"),
            GG_STR("aws.greengrass.NucleusLite"),
            GG_STR("configuration"),
            GG_STR("networkProxy"),
            GG_STR("noProxyAddresses")
        ),
        alloc,
        out
    );
    if (ret == GG_ERR_NOENTRY) {
        *found = false;
        return GG_ERR_OK;
    }
    if (ret != GG_ERR_OK) {
        return ret;
    }

    *found = true;
    return GG_ERR_OK;
}

static bool no_proxy_matches(GgBuffer hostname, GgBuffer no_proxy) {
    while (no_proxy.len > 0) {
        // skip separators
        while (no_proxy.len > 0
               && (no_proxy.data[0] == ',' || no_proxy.data[0] == ' ')) {
            no_proxy = gg_buffer_substr(no_proxy, 1, no_proxy.len);
        }
        // find end of entry
        size_t end = 0;
        while (end < no_proxy.len && no_proxy.data[end] != ',') {
            end++;
        }
        GgBuffer entry = gg_buffer_substr(no_proxy, 0, end);
        no_proxy = gg_buffer_substr(no_proxy, end, no_proxy.len);

        // trim trailing spaces
        while (entry.len > 0 && entry.data[entry.len - 1] == ' ') {
            entry.len--;
        }
        if (entry.len == 0) {
            continue;
        }
        if (gg_buffer_eq(entry, GG_STR("*"))) {
            return true;
        }
        if (gg_buffer_eq(hostname, entry)) {
            return true;
        }
        // suffix match at domain boundary
        if (gg_buffer_has_suffix(hostname, entry)
            && (entry.data[0] == '.'
                || hostname.data[hostname.len - entry.len - 1] == '.')) {
            return true;
        }
    }
    return false;
}

static GgError tcp_connect(GgBuffer host, GgBuffer port, int *out_fd) {
    // getaddrinfo needs null-terminated strings
    char host_z[256];
    char port_z[6];
    if (host.len >= sizeof(host_z) || port.len >= sizeof(port_z)) {
        GG_LOGE("Host or port too long.");
        return GG_ERR_NOMEM;
    }
    memcpy(host_z, host.data, host.len);
    host_z[host.len] = '\0';
    memcpy(port_z, port.data, port.len);
    port_z[port.len] = '\0';

    struct addrinfo hints = { .ai_socktype = SOCK_STREAM };
    struct addrinfo *res = NULL;
    if (getaddrinfo(host_z, port_z, &hints, &res) != 0) {
        GG_LOGE("Failed to resolve %s:%s.", host_z, port_z);
        return GG_ERR_FAILURE;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
        fd = socket(
            ai->ai_family, ai->ai_socktype | SOCK_CLOEXEC, ai->ai_protocol
        );
        if (fd < 0) {
            continue;
        }
        if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
            break;
        }
        (void) gg_close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        GG_LOGE("Failed to connect to %s:%s.", host_z, port_z);
        return GG_ERR_FAILURE;
    }

    *out_fd = fd;
    return GG_ERR_OK;
}

/// Read from fd into vec until vec contains \r\n.
static GgError read_until_crlf(int fd, GgByteVec *vec) {
    for (;;) {
        GgBuffer rest = gg_byte_vec_remaining_capacity(*vec);
        if (rest.len == 0) {
            GG_LOGE("Failed to read proxy status line; line too long?");
            return GG_ERR_NOMEM;
        }
        size_t old_len = vec->buf.len;
        GgError ret = gg_file_read_partial(fd, &rest);
        if (ret != GG_ERR_OK) {
            return ret;
        }
        vec->buf.len = (size_t) (rest.data - vec->buf.data);
        // Back up one byte to catch \r\n split across reads.
        size_t start = (old_len > 0) ? old_len - 1 : 0;
        GgBuffer new_data = gg_buffer_substr(vec->buf, start, vec->buf.len);
        if (gg_buffer_contains(new_data, GG_STR("\r\n"), NULL)) {
            return GG_ERR_OK;
        }
    }
}

/// Drain fd until \r\n\r\n. Compacts vec when full, keeping last 3 bytes.
/// Fails if any bytes are read past the end of headers.
static GgError drain_headers(int fd, GgByteVec *vec) {
    for (;;) {
        size_t pos = 0;
        if (gg_buffer_contains(vec->buf, GG_STR("\r\n\r\n"), &pos)) {
            if (pos + 4 != vec->buf.len) {
                GG_LOGE("Proxy sent data after end of headers; "
                        "check proxy server configuration.");
                return GG_ERR_FAILURE;
            }
            return GG_ERR_OK;
        }
        if (vec->buf.len > 3) {
            memmove(vec->buf.data, vec->buf.data + vec->buf.len - 3, 3);
            vec->buf.len = 3;
        }
        GgBuffer rest = gg_byte_vec_remaining_capacity(*vec);
        GgError ret = gg_file_read_partial(fd, &rest);
        if (ret != GG_ERR_OK) {
            return ret;
        }
        vec->buf.len = (size_t) (rest.data - vec->buf.data);
    }
}

static GgError http_connect_tunnel(int fd, GgBuffer host, GgBuffer port) {
    // Build CONNECT request
    // "CONNECT " + host(253) + ":" + port(5) + " HTTP/1.1\r\n" = 278
    // "Host: " + host(253) + ":" + port(5) + "\r\n\r\n" = 269; total = 547
    uint8_t req_mem[548];
    GgByteVec req = GG_BYTE_VEC(req_mem);
    GgError err = GG_ERR_OK;
    gg_byte_vec_chain_append(&err, &req, GG_STR("CONNECT "));
    gg_byte_vec_chain_append(&err, &req, host);
    gg_byte_vec_chain_append(&err, &req, GG_STR(":"));
    gg_byte_vec_chain_append(&err, &req, port);
    gg_byte_vec_chain_append(&err, &req, GG_STR(" HTTP/1.1\r\nHost: "));
    gg_byte_vec_chain_append(&err, &req, host);
    gg_byte_vec_chain_append(&err, &req, GG_STR(":"));
    gg_byte_vec_chain_append(&err, &req, port);
    gg_byte_vec_chain_append(&err, &req, GG_STR("\r\n\r\n"));
    if (err != GG_ERR_OK) {
        GG_LOGE("CONNECT request too long.");
        return err;
    }

    GgError ret = gg_file_write(fd, req.buf);
    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to send CONNECT request.");
        return ret;
    }

    // Read status line. Reuse req_mem since request is sent.
    uint8_t resp_mem[64];
    GgByteVec resp = GG_BYTE_VEC(resp_mem);
    ret = read_until_crlf(fd, &resp);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    // Validate "HTTP/1.x 200"
    if (!gg_buffer_has_prefix(resp.buf, GG_STR("HTTP/1."))) {
        GG_LOGE("Invalid proxy response.");
        return GG_ERR_FAILURE;
    }
    GgBuffer code = gg_buffer_substr(resp.buf, 9, 12);
    if (!gg_buffer_eq(code, GG_STR("200"))) {
        GG_LOGE("Proxy CONNECT failed: %.*s", (int) code.len, code.data);
        return GG_ERR_FAILURE;
    }

    // Drain remaining headers
    ret = drain_headers(fd, &resp);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GG_LOGD("HTTP proxy tunnel established.");
    return GG_ERR_OK;
}

GgError proxy_connect(const char *hostname, uint16_t port, int *out_fd) {
    uint8_t url_mem[4096];
    GgArena url_alloc = gg_arena_init(GG_BUF(url_mem));
    GgBuffer proxy_url = { 0 };
    bool have_proxy = false;

    GgError ret = get_proxy_url(&url_alloc, &proxy_url, &have_proxy);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    GgBuffer host = gg_buffer_from_null_term((char *) hostname);
    char port_str[6];
    int port_len = snprintf(port_str, sizeof(port_str), "%" PRIu16, port);
    GgBuffer port_buf
        = { .data = (uint8_t *) port_str, .len = (size_t) port_len };

    if (!have_proxy) {
        GG_LOGD("Connecting directly to %s:%" PRIu16 ".", hostname, port);
        return tcp_connect(host, port_buf, out_fd);
    }

    uint8_t np_mem[4096];
    GgArena np_alloc = gg_arena_init(GG_BUF(np_mem));
    GgBuffer no_proxy = { 0 };
    bool have_no_proxy = false;

    ret = get_no_proxy(&np_alloc, &no_proxy, &have_no_proxy);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    if (have_no_proxy && no_proxy_matches(host, no_proxy)) {
        GG_LOGD("Connecting directly to %s:%" PRIu16 ".", hostname, port);
        return tcp_connect(host, port_buf, out_fd);
    }

    // Parse proxy URL
    uint8_t uri_mem[512];
    GgArena uri_alloc = gg_arena_init(GG_BUF(uri_mem));
    GglUriInfo info = { 0 };
    ret = gg_uri_parse(&uri_alloc, proxy_url, &info);    if (ret != GG_ERR_OK) {
        GG_LOGE("Failed to parse proxy URL.");
        return ret;
    }

    if (info.host.len == 0) {
        GG_LOGE("No host in proxy URL.");
        return GG_ERR_INVALID;
    }

    if (info.scheme.len != 0 && !gg_buffer_eq(info.scheme, GG_STR("http"))) {
        GG_LOGE(
            "Unsupported proxy scheme: %.*s.",
            (int) info.scheme.len,
            info.scheme.data
        );
        return GG_ERR_INVALID;
    }

    GgBuffer proxy_port = info.port.len > 0 ? info.port : GG_STR("80");

    GG_LOGD(
        "Connecting to HTTP proxy %.*s:%.*s.",
        (int) info.host.len,
        info.host.data,
        (int) proxy_port.len,
        proxy_port.data
    );

    int fd = -1;
    ret = tcp_connect(info.host, proxy_port, &fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    ret = http_connect_tunnel(fd, host, port_buf);
    if (ret != GG_ERR_OK) {
        (void) gg_close(fd);
        return ret;
    }

    *out_fd = fd;
    return GG_ERR_OK;
}
