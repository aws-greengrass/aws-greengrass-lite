// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

// NOLINTNEXTLINE(readability-identifier-naming)
#define _GNU_SOURCE

#include "ggipc/auth.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <ggl/alloc.h>
#include <ggl/base64.h>
#include <ggl/buffer.h>
#include <ggl/bump_alloc.h>
#include <ggl/defer.h>
#include <ggl/error.h>
#include <ggl/log.h>
#include <ggl/object.h>
#include <ggl/socket.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <systemd/sd-login.h>
#include <unistd.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef GGL_IPC_AUTH_DISABLE
#warning "INSECURE!!! IPC authentication disabled!"

__attribute__((constructor)) static void warn_auth_disabled(void) {
    GGL_LOGE("ggipcd", "INSECURE!!! IPC authentication disabled!");
    GGL_LOGE("ggipcd", "SVCUID handling is in debug mode.");
}
#endif

/// Maximum number of generic components that can be authenticated.
/// Can be configured with `-DGGL_MAX_GENERIC_COMPONENTS=<N>`.
#ifndef GGL_MAX_GENERIC_COMPONENTS
#define GGL_MAX_GENERIC_COMPONENTS 50
#endif

/// Maximum length of generic component name.
#define GGL_MAX_COMPONENT_NAME_LENGTH 128

#define SVCUID_BYTES 12

static uint8_t svcuids[GGL_MAX_GENERIC_COMPONENTS][SVCUID_BYTES];
static uint8_t component_names[GGL_MAX_GENERIC_COMPONENTS]
                              [GGL_MAX_COMPONENT_NAME_LENGTH];
static size_t component_name_lengths[GGL_MAX_GENERIC_COMPONENTS];

static size_t registered_components = 0;

static void *auth_server_thread(void *args);

__attribute__((constructor)) static void start_auth_thread(void) {
    pthread_t sub_thread = { 0 };
    int sys_ret = pthread_create(&sub_thread, NULL, auth_server_thread, NULL);
    if (sys_ret != 0) {
        GGL_LOGE("ipc-auth-server", "Failed to create IPC auth server thread.");
        // exit() is not re-entrant and this is safe as long as no spawned
        // thread can call exit()
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        exit(-1);
    }
    pthread_detach(sub_thread);
}

GglError ggl_ipc_auth_get_component_name(
    GglBuffer svcuid, GglBuffer *component_name
) {
    assert(component_name != NULL);

#ifndef GGL_IPC_AUTH_DISABLE
    // Match decoded SVCUID and return match's name

    if (svcuid.len != (((size_t) SVCUID_BYTES / 3) * 4)) {
        GGL_LOGE("ipc-auth", "svcuid is invalid length.");
        return GGL_ERR_INVALID;
    }

    GglBuffer svcuid_b = GGL_BUF((uint8_t[SVCUID_BYTES]) { 0 });
    bool decoded = ggl_base64_decode(svcuid, &svcuid_b);
    if (!decoded) {
        GGL_LOGE("ipc-auth", "svcuid is invalid base64.");
        return GGL_ERR_INVALID;
    }

    for (size_t i = 0; i < registered_components; i++) {
        GglBuffer svcuid_i = { .data = svcuids[i], .len = SVCUID_BYTES };
        if (ggl_buffer_eq(svcuid_b, svcuid_i)) {
            *component_name = (GglBuffer) { .data = component_names[i],
                                            .len = component_name_lengths[i] };
            return GGL_ERR_OK;
        }
    }

    GGL_LOGE("ipc-auth", "Requested svcuid not registered.");
    return GGL_ERR_NOENTRY;
#else
    // Match name, and return stored name. Insert if new.
    // We need to return stored copy, as caller may assume output has static
    // lifetime.

    if (svcuid.len > GGL_MAX_COMPONENT_NAME_LENGTH) {
        GGL_LOGE("ipc-auth", "svcuid is invalid length.");
        return GGL_ERR_INVALID;
    }

    for (size_t i = 0; i < registered_components; i++) {
        GglBuffer component_name_i
            = { .data = component_names[i], .len = component_name_lengths[i] };
        if (ggl_buffer_eq(svcuid, component_name_i)) {
            *component_name = component_name_i;
            return GGL_ERR_OK;
        }
    }

    if (registered_components < GGL_MAX_GENERIC_COMPONENTS) {
        memcpy(component_names[registered_components], svcuid.data, svcuid.len);
        component_name_lengths[registered_components] = svcuid.len;
        *component_name = (GglBuffer
        ) { .data = component_names[registered_components], .len = svcuid.len };
        registered_components += 1;
        return GGL_ERR_OK;
    }

    GGL_LOGE("ipc-auth", "Insufficent generic component slots.");
    return GGL_ERR_NOENTRY;
#endif
}

// TODO: Clean up this function
// NOLINTNEXTLINE(readability-function-cognitive-complexity)
static GglError run_svcuid_server(void) {
    int server_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (server_fd == -1) {
        int err = errno;
        GGL_LOGE("ipc-auth-server", "Failed to create socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    struct sockaddr_un addr
        = { .sun_family = AF_UNIX, .sun_path = "./gg-ipc-auth.socket" };

    if ((unlink(addr.sun_path) == -1) && (errno != ENOENT)) {
        int err = errno;
        GGL_LOGE("ipc-auth-server", "Failed to unlink server socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    if (bind(server_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        int err = errno;
        GGL_LOGE("ipc-auth-server", "Failed to bind server socket: %d.", err);
        return GGL_ERR_FAILURE;
    }

    static const int MAX_SOCKET_BACKLOG = 10;
    if (listen(server_fd, MAX_SOCKET_BACKLOG) == -1) {
        int err = errno;
        GGL_LOGE(
            "ipc-auth-server", "Failed to listen on server socket: %d.", err
        );
        return GGL_ERR_FAILURE;
    }

    int urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd == -1) {
        int err = errno;
        GGL_LOGE("ipc-auth-server", "Failed to open /dev/urandom: %d.", err);
        return GGL_ERR_FAILURE;
    }

    while (true) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) {
            int err = errno;
            GGL_LOGE(
                "ipc-auth-server",
                "Failed to accept on socket %d: %d.",
                server_fd,
                err
            );
            continue;
        }
        GGL_DEFER(close, client_fd);

        GGL_LOGD("ipc-auth-server", "Accepted new client %d.", client_fd);

        fcntl(client_fd, F_SETFD, FD_CLOEXEC);

        // To prevent deadlocking on hanged client, add a timeout
        struct timeval timeout = { .tv_sec = 5 };
        setsockopt(
            client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)
        );
        setsockopt(
            client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)
        );

        struct ucred ucred;

        socklen_t ucred_len = sizeof(ucred);
        if ((getsockopt(client_fd, SOL_SOCKET, SO_PEERCRED, &ucred, &ucred_len)
             != 0)
            || (ucred_len != sizeof(ucred))) {
            GGL_LOGE(
                "ipc-auth-server", "Failed to get peer cred for %d.", client_fd
            );
            continue;
        }

        char *unit_name = NULL;
        int error = sd_pid_get_unit(ucred.pid, &unit_name);
        char *component_name = unit_name;
        GGL_DEFER(free, unit_name);

        if ((error < 0) || (unit_name == NULL)) {
            GGL_LOGE(
                "ipc-auth-server",
                "Failed to look up service for pid %d.",
                ucred.pid
            );
            continue;
        }

        size_t component_name_len = strlen(component_name);

        if ((component_name_len <= sizeof(".service"))
            || (memcmp(
                    &component_name
                        [component_name_len - (sizeof(".service") - 1)],
                    ".service",
                    sizeof(".service") - 1
                )
                != 0)) {
            GGL_LOGE(
                "ipc-auth-server",
                "Service for pid %d (%s) missing service extension.",
                ucred.pid,
                unit_name
            );
            continue;
        }

        component_name_len -= sizeof(".service") - 1;

        if (component_name_len > GGL_MAX_COMPONENT_NAME_LENGTH) {
            GGL_LOGE(
                "ipc-auth-server",
                "Component name %.*s is too long.",
                (int) component_name_len,
                component_name
            );
            continue;
        }

        GglBuffer svcuid = { 0 };
        bool svcuid_found = false;

        for (size_t i = 0; i < registered_components; i++) {
            if ((component_name_lengths[i] == component_name_len)
                && (memcmp(
                        component_names[i], component_name, component_name_len
                    )
                    == 0)) {
                svcuid = GGL_BUF(svcuids[i]);
                svcuid_found = true;
                GGL_LOGD(
                    "ipc-auth-server",
                    "Found existing svcuid for component %.*s.",
                    (int) component_name_len,
                    component_name
                );
                break;
            }
        }

        if (!svcuid_found) {
            if (registered_components >= GGL_MAX_GENERIC_COMPONENTS) {
                GGL_LOGE("ipc-auth", "Insufficent generic component slots.");
                return GGL_ERR_NOMEM;
            }

            GGL_LOGD(
                "ipc-auth-server",
                "Registering new svcuid for component %.*s.",
                (int) component_name_len,
                component_name
            );
            svcuid = GGL_BUF(svcuids[registered_components]);
            GglError ret = ggl_read_exact(urandom_fd, svcuid);
            if (ret != GGL_ERR_OK) {
                GGL_LOGE(
                    "ipc-auth-server", "Failed to read from /dev/urandom."
                );
                return GGL_ERR_FATAL;
            }

            memcpy(
                component_names[registered_components],
                component_name,
                component_name_len
            );
            component_name_lengths[registered_components] = component_name_len;
            registered_components += 1;
        }

        GGL_LOGD(
            "ipc-auth-server",
            "Authenticated process %d for component %.*s.",
            ucred.pid,
            (int) component_name_len,
            component_name
        );

        GglBuffer svcuid_mem = GGL_BUF((uint8_t[(SVCUID_BYTES / 3) * 4]) { 0 });
        GglBumpAlloc balloc = ggl_bump_alloc_init(svcuid_mem);
        GglBuffer svcuid_b64;
        GglError ret = ggl_base64_encode(svcuid, &balloc.alloc, &svcuid_b64);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ipc-auth-server", "Failed to encode svcuid in base64.");
            assert(false);
            return GGL_ERR_FATAL;
        }

        ret = ggl_write_exact(client_fd, svcuid_b64);
        if (ret != GGL_ERR_OK) {
            GGL_LOGE("ipc-auth-server", "Failed to write to client.");
            continue;
        }
    }
}

static void *auth_server_thread(void *args) {
    (void) args;
    GGL_LOGD("ipc-auth-server", "Started IPC auth server thread.");
    (void) run_svcuid_server();
    GGL_LOGE("ipc-auth-server", "IPC auth server thread exited.");
    return NULL;
}
