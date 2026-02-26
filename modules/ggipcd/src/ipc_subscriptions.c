// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "ipc_subscriptions.h"
#include "ipc_server.h"
#include <assert.h>
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/log.h>
#include <gg/object.h>
#include <ggl/core_bus/client.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GGL_IPC_MAX_SUBSCRIPTIONS GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS

static_assert(
    GGL_IPC_MAX_SUBSCRIPTIONS <= GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS,
    "IPC max subscriptions exceededs core bus max subscriptions."
);

static uint32_t subs_resp_handle[GGL_IPC_MAX_SUBSCRIPTIONS];
static int32_t subs_stream_id[GGL_IPC_MAX_SUBSCRIPTIONS];
static uint32_t subs_recv_handle[GGL_IPC_MAX_SUBSCRIPTIONS];
static pthread_mutex_t subs_state_mtx = PTHREAD_MUTEX_INITIALIZER;

static GgError init_subs_index(
    uint32_t resp_handle, int32_t stream_id, size_t *index
) {
    assert(resp_handle != 0);

    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    for (size_t i = 0; i < GGL_IPC_MAX_SUBSCRIPTIONS; i++) {
        if (subs_resp_handle[i] == 0) {
            subs_resp_handle[i] = resp_handle;
            subs_stream_id[i] = stream_id;
            *index = i;
            return GG_ERR_OK;
        }
    }

    GG_LOGE("Exceeded maximum tracked subscriptions.");
    return GG_ERR_NOMEM;
}

static void release_subs_index(size_t index, uint32_t resp_handle) {
    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    if (subs_resp_handle[index] == resp_handle) {
        subs_resp_handle[index] = 0;
        subs_stream_id[index] = 0;
        subs_recv_handle[index] = 0;
    } else {
        GG_LOGD("Releasing subscription state failed; already released.");
    }
}

static GgError subs_set_recv_handle(
    size_t index, uint32_t resp_handle, uint32_t recv_handle
) {
    assert(resp_handle != 0);
    assert(recv_handle != 0);

    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    if (subs_resp_handle[index] == resp_handle) {
        subs_recv_handle[index] = recv_handle;
        return GG_ERR_OK;
    }

    GG_LOGD("Setting subscription recv handle failed; state already released.");
    return GG_ERR_FAILURE;
}

static GgError subscription_on_response(
    void *ctx, uint32_t recv_handle, GgObject data
) {
    // coverity[bad_initializer_type]
    GglIpcSubscribeCallback on_response = ctx;

    uint32_t resp_handle = 0;
    int32_t stream_id = -1;
    bool found = false;

    {
        GG_MTX_SCOPE_GUARD(&subs_state_mtx);
        for (size_t i = 0; i < GGL_IPC_MAX_SUBSCRIPTIONS; i++) {
            if (recv_handle == subs_recv_handle[i]) {
                found = true;
                resp_handle = subs_resp_handle[i];
                stream_id = subs_stream_id[i];
                break;
            }
        }
    }

    if (!found) {
        GG_LOGD("Received response on released subscription.");
        return GG_ERR_FAILURE;
    }

    static uint8_t resp_mem
        [sizeof(GgObject[GG_MAX_OBJECT_SUBOBJECTS]) + GGL_IPC_MAX_MSG_LEN];
    GgArena alloc = gg_arena_init(GG_BUF(resp_mem));

    return on_response(data, resp_handle, stream_id, &alloc);
}

static void subscription_on_close(void *ctx, uint32_t recv_handle) {
    (void) ctx;
    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    for (size_t i = 0; i < GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS; i++) {
        if (recv_handle == subs_recv_handle[i]) {
            subs_resp_handle[i] = 0;
            subs_stream_id[i] = 0;
            subs_recv_handle[i] = 0;
            return;
        }
    }

    GG_LOGD("Already released subscription closed.");
}

GgError ggl_ipc_bind_subscription(
    uint32_t resp_handle,
    int32_t stream_id,
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglIpcSubscribeCallback on_response,
    GgError *error
) {
    size_t subs_index = 0;
    GgError ret = init_subs_index(resp_handle, stream_id, &subs_index);
    if (ret != GG_ERR_OK) {
        return ret;
    }

    uint32_t recv_handle = 0;
    ret = ggl_subscribe(
        interface,
        method,
        params,
        subscription_on_response,
        subscription_on_close,
        on_response,
        error,
        &recv_handle
    );
    if (ret != GG_ERR_OK) {
        release_subs_index(subs_index, resp_handle);
        return ret;
    }

    (void) subs_set_recv_handle(subs_index, resp_handle, recv_handle);

    return GG_ERR_OK;
}

GgError ggl_ipc_release_subscriptions_for_conn(uint32_t resp_handle) {
    for (size_t i = 0; i < GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS; i++) {
        uint32_t recv_handle = 0;

        {
            GG_MTX_SCOPE_GUARD(&subs_state_mtx);

            if (subs_resp_handle[i] == resp_handle) {
                recv_handle = subs_recv_handle[i];
            }
        }

        if (recv_handle != 0) {
            ggl_client_sub_close(recv_handle);
        }
    }

    return GG_ERR_OK;
}

void ggl_ipc_terminate_stream(uint32_t resp_handle, int32_t stream_id) {
    for (size_t i = 0; i < GGL_COREBUS_CLIENT_MAX_SUBSCRIPTIONS; i++) {
        uint32_t recv_handle = 0;

        {
            GG_MTX_SCOPE_GUARD(&subs_state_mtx);

            if ((subs_resp_handle[i] == resp_handle)
                && (subs_stream_id[i] == stream_id)) {
                recv_handle = subs_recv_handle[i];
            }
        }

        if (recv_handle != 0) {
            ggl_client_sub_close(recv_handle);
            return;
        }
    }
}
