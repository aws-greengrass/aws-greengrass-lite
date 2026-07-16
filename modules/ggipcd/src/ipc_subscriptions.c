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
    "IPC subscription maximum exceeds core bus maximum."
);

typedef struct {
    uint32_t resp_handle;
    int32_t stream_id;
    uint32_t recv_handle;
    GglIpcSubscribeCallback on_response;
    void *ctx;
    bool binding;
    bool close_requested;
    bool core_closed;
} IpcSubscriptionSlot;

static IpcSubscriptionSlot subscriptions[GGL_IPC_MAX_SUBSCRIPTIONS];
static pthread_mutex_t subs_state_mtx = PTHREAD_MUTEX_INITIALIZER;

static void reset_sub_slot(IpcSubscriptionSlot *slot) {
    *slot = (IpcSubscriptionSlot) { 0 };
}

static GgError reserve_sub_slot(
    uint32_t resp_handle,
    int32_t stream_id,
    GglIpcSubscribeCallback on_response,
    void *ctx,
    IpcSubscriptionSlot **result
) {
    assert(resp_handle != 0);
    assert(on_response != NULL);

    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    for (size_t i = 0; i < GGL_IPC_MAX_SUBSCRIPTIONS; i++) {
        IpcSubscriptionSlot *slot = &subscriptions[i];
        if (slot->resp_handle == 0) {
            *slot = (IpcSubscriptionSlot) {
                .resp_handle = resp_handle,
                .stream_id = stream_id,
                .on_response = on_response,
                .ctx = ctx,
                .binding = true,
            };
            *result = slot;
            return GG_ERR_OK;
        }
    }

    GG_LOGE("Exceeded maximum tracked subscriptions.");
    return GG_ERR_NOMEM;
}

static void release_binding_slot(IpcSubscriptionSlot *slot) {
    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    assert(slot->binding);
    reset_sub_slot(slot);
}

static GgError subscription_on_response(
    void *ctx, uint32_t recv_handle, GgObject data
) {
    IpcSubscriptionSlot *slot = ctx;

    uint32_t resp_handle = 0;
    int32_t stream_id = -1;
    GglIpcSubscribeCallback on_response = NULL;
    void *user_ctx = NULL;

    {
        GG_MTX_SCOPE_GUARD(&subs_state_mtx);

        if (slot->resp_handle == 0) {
            GG_LOGD("Received response on released subscription.");
            return GG_ERR_FAILURE;
        }

        if (slot->recv_handle == 0) {
            slot->recv_handle = recv_handle;
        } else if (slot->recv_handle != recv_handle) {
            GG_LOGE("Unexpected subscription response handle.");
            return GG_ERR_FAILURE;
        }

        if (slot->close_requested || slot->core_closed) {
            GG_LOGD("Received response while subscription is closing.");
            return GG_ERR_FAILURE;
        }

        resp_handle = slot->resp_handle;
        stream_id = slot->stream_id;
        on_response = slot->on_response;
        user_ctx = slot->ctx;
    }

    static uint8_t resp_mem
        [sizeof(GgObject[GG_MAX_OBJECT_SUBOBJECTS]) + GGL_IPC_MAX_MSG_LEN];
    GgArena alloc = gg_arena_init(GG_BUF(resp_mem));

    return on_response(user_ctx, data, resp_handle, stream_id, &alloc);
}

static void subscription_on_close(void *ctx, uint32_t recv_handle) {
    IpcSubscriptionSlot *slot = ctx;

    GG_MTX_SCOPE_GUARD(&subs_state_mtx);

    if (slot->resp_handle == 0) {
        GG_LOGD("Already released subscription closed.");
        return;
    }

    if ((slot->recv_handle != 0) && (slot->recv_handle != recv_handle)) {
        GG_LOGE("Closed an unexpected subscription handle.");
        // No further close will arrive for this slot; release it rather than
        // leak it permanently. Mirrors the normal close path below, without
        // adopting the mismatched handle.
        slot->close_requested = true;
        slot->core_closed = true;
        if (!slot->binding) {
            reset_sub_slot(slot);
        }
        return;
    }

    slot->recv_handle = recv_handle;
    slot->close_requested = true;
    slot->core_closed = true;

    // ggl_subscribe may invoke this callback before returning the handle. Keep
    // the slot reserved until the binding call no longer holds its pointer.
    if (!slot->binding) {
        reset_sub_slot(slot);
    }
}

GgError ggl_ipc_bind_subscription(
    uint32_t resp_handle,
    int32_t stream_id,
    GgBuffer interface,
    GgBuffer method,
    GgMap params,
    GglIpcSubscribeCallback on_response,
    void *ctx,
    GgError *error
) {
    IpcSubscriptionSlot *slot = NULL;
    GgError ret
        = reserve_sub_slot(resp_handle, stream_id, on_response, ctx, &slot);
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
        slot,
        error,
        &recv_handle
    );
    if (ret != GG_ERR_OK) {
        release_binding_slot(slot);
        return ret;
    }

    bool close_requested = false;
    bool core_closed = false;
    {
        GG_MTX_SCOPE_GUARD(&subs_state_mtx);

        assert(slot->binding);
        assert((slot->recv_handle == 0) || (slot->recv_handle == recv_handle));

        slot->recv_handle = recv_handle;
        slot->binding = false;
        close_requested = slot->close_requested;
        core_closed = slot->core_closed;

        if (core_closed) {
            reset_sub_slot(slot);
        }
    }

    if (close_requested && !core_closed) {
        ggl_client_sub_close(recv_handle);
    }

    return GG_ERR_OK;
}

GgError ggl_ipc_release_subscriptions_for_conn(uint32_t resp_handle) {
    for (size_t i = 0; i < GGL_IPC_MAX_SUBSCRIPTIONS; i++) {
        uint32_t recv_handle = 0;

        {
            GG_MTX_SCOPE_GUARD(&subs_state_mtx);

            IpcSubscriptionSlot *slot = &subscriptions[i];
            if (slot->resp_handle == resp_handle) {
                slot->close_requested = true;
                if (!slot->core_closed) {
                    recv_handle = slot->recv_handle;
                }
            }
        }

        if (recv_handle != 0) {
            ggl_client_sub_close(recv_handle);
        }
    }

    return GG_ERR_OK;
}

void ggl_ipc_terminate_stream(uint32_t resp_handle, int32_t stream_id) {
    for (size_t i = 0; i < GGL_IPC_MAX_SUBSCRIPTIONS; i++) {
        uint32_t recv_handle = 0;

        {
            GG_MTX_SCOPE_GUARD(&subs_state_mtx);

            IpcSubscriptionSlot *slot = &subscriptions[i];
            if ((slot->resp_handle == resp_handle)
                && (slot->stream_id == stream_id)) {
                slot->close_requested = true;
                if (!slot->core_closed) {
                    recv_handle = slot->recv_handle;
                }
            }
        }

        if (recv_handle != 0) {
            ggl_client_sub_close(recv_handle);
            return;
        }
    }
}
