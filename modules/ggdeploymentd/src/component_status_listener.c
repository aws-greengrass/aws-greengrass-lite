// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "component_status_listener.h"
#include "deployment_handler.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <gg/types.h>
#include <gg/utils.h>
#include <ggl/core_bus/client.h>
#include <ggl/core_bus/gg_healthd.h>
#include <inttypes.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
// Seconds to wait before (re)attempting a subscription, and the poll interval
// used to notice that the subscription dropped.
#define RESUBSCRIBE_BACKOFF_SECONDS 5

// Set by the subscription close callback (on the core-bus subscription thread)
// and observed by the supervisor thread to trigger a re-subscribe.
static atomic_bool subscription_closed;

// Forwards a component status change to the fleet status service. FSS gathers
// the full component list itself; we only need to trigger a PARTIAL update.
static void forward_to_fleet_status(void) {
    static uint8_t resp_mem[128];
    GgArena alloc = gg_arena_init(GG_BUF(resp_mem));
    GgObject result;

    GgMap args = GG_MAP(
        gg_kv(GG_STR("trigger"), gg_obj_buf(GG_STR("COMPONENT_STATUS_CHANGE"))),
        gg_kv(GG_STR("deployment_info"), gg_obj_map(GG_MAP()))
    );

    GgError ret = ggl_call(
        GG_STR("gg_fleet_status"),
        GG_STR("send_fleet_status_update"),
        args,
        NULL,
        &alloc,
        &result
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to forward component status change to fleet status "
            "service (ret=%d).",
            (int) ret
        );
    }
}

// Runs on the core-bus subscription thread for each broadcast notification.
// Always returns GG_ERR_OK: returning an error would tear down the
// subscription, and a single failed forward should not do that.
static GgError on_state_change(void *ctx, uint32_t handle, GgObject data) {
    (void) ctx;
    (void) handle;

    if (gg_obj_type(data) != GG_TYPE_MAP) {
        GG_LOGW("Component state change notification is not a map.");
        return GG_ERR_OK;
    }

    GgObject *component_name_obj = NULL;
    GgObject *state_obj = NULL;
    GgError ret = gg_map_validate(
        gg_obj_into_map(data),
        GG_MAP_SCHEMA(
            { GG_STR("component_name"),
              GG_REQUIRED,
              GG_TYPE_BUF,
              &component_name_obj },
            { GG_STR("lifecycle_state"), GG_REQUIRED, GG_TYPE_BUF, &state_obj }
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGW("Component state change notification missing fields.");
        return GG_ERR_OK;
    }

    GgBuffer component_name = gg_obj_into_buf(*component_name_obj);
    GgBuffer status = gg_obj_into_buf(*state_obj);

    if (ggl_deployment_in_progress()) {
        GG_LOGD(
            "Deployment in progress; not forwarding %.*s -> %.*s.",
            (int) component_name.len,
            component_name.data,
            (int) status.len,
            status.data
        );
        return GG_ERR_OK;
    }

    GG_LOGD(
        "Forwarding component state change to fleet status service: "
        "%.*s -> %.*s.",
        (int) component_name.len,
        component_name.data,
        (int) status.len,
        status.data
    );
    forward_to_fleet_status();
    return GG_ERR_OK;
}

static void on_close(void *ctx, uint32_t handle) {
    (void) ctx;
    (void) handle;
    atomic_store(&subscription_closed, true);
}

static void *listener_thread(void *ctx) {
    (void) ctx;

    while (true) {
        atomic_store(&subscription_closed, false);

        uint32_t handle = 0;
        GgError remote_err = GG_ERR_OK;
        GgError ret = ggl_gghealthd_subscribe_to_all_component_state_changes(
            on_state_change, on_close, NULL, &remote_err, &handle
        );
        if (ret != GG_ERR_OK) {
            GG_LOGW(
                "Failed to subscribe to gghealthd component state changes "
                "(ret=%d, remote=%d). Retrying in %ds.",
                (int) ret,
                (int) remote_err,
                RESUBSCRIBE_BACKOFF_SECONDS
            );
            (void) gg_sleep(RESUBSCRIBE_BACKOFF_SECONDS);
            continue;
        }

        GG_LOGI(
            "Subscribed to gghealthd component state changes (handle=%" PRIu32
            ").",
            handle
        );

        while (!atomic_load(&subscription_closed)) {
            (void) gg_sleep(RESUBSCRIBE_BACKOFF_SECONDS);
        }

        GG_LOGW("gghealthd component state change subscription closed; "
                "resubscribing.");
        (void) gg_sleep(RESUBSCRIBE_BACKOFF_SECONDS);
    }

    return NULL;
}

void ggl_start_component_status_listener(void) {
    pthread_t ptid;
    int sys_ret = pthread_create(&ptid, NULL, listener_thread, NULL);
    if (sys_ret != 0) {
        GG_LOGE(
            "Failed to start component status listener thread: %d.", sys_ret
        );
        return;
    }
    pthread_detach(ptid);
    GG_LOGI("Started component status listener.");
}
