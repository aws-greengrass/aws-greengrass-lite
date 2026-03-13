// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "proxy.h"
#include <assert.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/file.h>
#include <gg/log.h>
#include <ggl/process.h>
#include <ggl/tls.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <stdbool.h>

#define CONTROL_FD 3
#define TRANSPORT_FD 4

static GgError receive_helper_fd(int sock, int *out_fd) {
    char payload[64];
    struct iovec iov = { .iov_base = payload, .iov_len = sizeof(payload) };

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

    ssize_t n = recvmsg(sock, &msg, 0);
    if ((n != 6) || (memcmp(payload, "socket", 6) != 0)) {
        GG_LOGE("Helper did not send expected payload.");
        return GG_ERR_FAILURE;
    }

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    if ((cmsg == NULL) || (cmsg->cmsg_level != SOL_SOCKET)
        || (cmsg->cmsg_type != SCM_RIGHTS)) {
        GG_LOGE("No SCM_RIGHTS from helper.");
        return GG_ERR_FAILURE;
    }

    memcpy(out_fd, CMSG_DATA(cmsg), sizeof(int));
    return GG_ERR_OK;
}

typedef struct {
    int ctl_fd;
    int transport_fd;
} ChildSetupCtx;

static GgError child_setup(void *ctx) {
    ChildSetupCtx *c = ctx;
    if (c->transport_fd == CONTROL_FD) {
        // transport_fd is 3; move it so dup2(ctl, 3) won't clobber it.
        c->transport_fd = dup(c->transport_fd);
        if (c->transport_fd < 0) {
            GG_LOGE("dup failed: %m.");
            return GG_ERR_FATAL;
        }
    }
    if (dup2(c->ctl_fd, CONTROL_FD) < 0) {
        GG_LOGE("dup2 ctl failed: %m.");
        return GG_ERR_FATAL;
    }
    if (dup2(c->transport_fd, TRANSPORT_FD) < 0) {
        GG_LOGE("dup2 transport failed: %m.");
        return GG_ERR_FATAL;
    }
    ggl_close_fds_from(5);
    return GG_ERR_OK;
}

static GgError tls_on_fd(
    int transport_fd,
    const char *hostname,
    const char *private_key,
    const char *certificate,
    const char *root_ca,
    GglTlsConn *conn
) {
    int ctl_sock[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, ctl_sock) < 0) {
        GG_LOGE("socketpair failed: %m.");
        return GG_ERR_FAILURE;
    }
    GG_CLEANUP(cleanup_close, ctl_sock[0]);

    GglProcessHandle proc = { 0 };
    GgError ret = ggl_process_spawn(
        (const char *[]) {
            "ggl-tls-helper", "--hostname",    hostname,    "--private-key",
            private_key,      "--certificate", certificate, "--root-ca",
            root_ca,          NULL,
        },
        &(GglProcessSpawnConfig) {
            .child_setup = child_setup,
            .child_setup_ctx = &(ChildSetupCtx) {
                .ctl_fd = ctl_sock[1],
                .transport_fd = transport_fd,
            },
            .keep_fds = true,
        },
        &proc
    );
    (void) gg_close(ctl_sock[1]);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    int tunnel_fd = -1;
    ret = receive_helper_fd(ctl_sock[0], &tunnel_fd);
    if (ret != GG_ERR_OK) {
        (void) ggl_process_kill(proc, 0);
        return ret;
    }

    *conn = (GglTlsConn) { .fd = tunnel_fd, .pid = proc.val };
    return GG_ERR_OK;
}

GgError ggl_tls_connect(
    const char *hostname,
    uint16_t port,
    const char *private_key,
    const char *certificate,
    const char *root_ca,
    GglTlsConn *conn
) {
    int fd = -1;
    GgError ret = proxy_connect(hostname, port, &fd);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    GG_CLEANUP(cleanup_close, fd);

    ret = tls_on_fd(fd, hostname, private_key, certificate, root_ca, conn);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    return GG_ERR_OK;
}

GgError ggl_tls_read(GglTlsConn *conn, GgBuffer *buf) {
    GgBuffer rest = *buf;
    GgError ret = gg_file_read_partial(conn->fd, &rest);
    if (ret != GG_ERR_OK) {
        return ret;
    }
    buf->len = (size_t) (rest.data - buf->data);
    return GG_ERR_OK;
}

GgError ggl_tls_write(GglTlsConn *conn, GgBuffer buf) {
    return gg_file_write(conn->fd, buf);
}

int ggl_tls_get_fd(GglTlsConn *conn) {
    return conn->fd;
}

GgError ggl_tls_close(GglTlsConn *conn) {
    assert(conn->fd >= 0);
    (void) gg_close(conn->fd);
    conn->fd = -1;

    GglProcessHandle proc = { .val = conn->pid };
    conn->pid = 0;
    return ggl_process_kill(proc, 1);
}
