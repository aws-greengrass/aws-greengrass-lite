// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include "status_keeper.h"
#include <gg/arena.h>
#include <gg/buffer.h>
#include <gg/cleanup.h>
#include <gg/error.h>
#include <gg/flags.h>
#include <gg/log.h>
#include <gg/map.h>
#include <gg/object.h>
#include <ggl/core_bus/gg_config.h>
#include <pthread.h>
#include <sys/types.h>
#include <stdbool.h>
#include <stddef.h>

// ggconfigd key path for the pending deployment-status slot.
#define PENDING_STATUS_KEY_PATH \
    GG_BUF_LIST( \
        GG_STR("services"), \
        GG_STR("DeploymentService"), \
        GG_STR("pendingStatus") \
    )

// Guards the config slot and the slot_pending hint. update_job_to can be
// reached from both the deployment-handler thread and the job-listener thread,
// so all operations are serialized here. ggconfigd serializes the underlying
// config ops, but the in-memory hint still needs guarding.
static pthread_mutex_t slot_mtx = PTHREAD_MUTEX_INITIALIZER;

// Cheap cache of "does a slot exist". Kept in sync by persist (set), read
// (set on hit), and clear (unset). Authoritative state is the config slot;
// this only lets the happy path avoid a config call.
static bool slot_pending = false;

GgError status_keeper_persist(GgBuffer job_id, GgBuffer status) {
    GG_MTX_SCOPE_GUARD(&slot_mtx);

    // NULL timestamp => ggconfigd stamps with current time (matches the
    // configuration-merge write in component_config.c).
    GgError ret = ggl_gg_config_write(
        PENDING_STATUS_KEY_PATH,
        gg_obj_map(GG_MAP(
            gg_kv(GG_STR("job_id"), gg_obj_buf(job_id)),
            gg_kv(GG_STR("status"), gg_obj_buf(status))
        )),
        NULL
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to persist pending deployment status: %s", gg_strerror(ret)
        );
        return ret;
    }

    slot_pending = true;
    GG_LOGD(
        "Persisted pending status %.*s for job %.*s.",
        (int) status.len,
        status.data,
        (int) job_id.len,
        job_id.data
    );
    return GG_ERR_OK;
}

GgError status_keeper_read(GgArena *alloc, GgBuffer *job_id, GgBuffer *status) {
    GG_MTX_SCOPE_GUARD(&slot_mtx);

    GgObject slot_obj;
    GgError ret = ggl_gg_config_read(PENDING_STATUS_KEY_PATH, alloc, &slot_obj);
    if (ret != GG_ERR_OK) {
        // Typically GG_ERR_NOENTRY: no slot stored. Leave the hint untouched on
        // a transient read error; only a confirmed write/clear changes it.
        return ret;
    }

    if (gg_obj_type(slot_obj) != GG_TYPE_MAP) {
        GG_LOGE("Pending status slot is not a map.");
        return GG_ERR_INVALID;
    }

    GgObject *job_id_obj = NULL;
    GgObject *status_obj = NULL;
    ret = gg_map_validate(
        gg_obj_into_map(slot_obj),
        GG_MAP_SCHEMA(
            { GG_STR("job_id"), GG_REQUIRED, GG_TYPE_BUF, &job_id_obj },
            { GG_STR("status"), GG_REQUIRED, GG_TYPE_BUF, &status_obj }
        )
    );
    if (ret != GG_ERR_OK) {
        GG_LOGE("Pending status slot is missing required fields.");
        return ret;
    }

    if (job_id != NULL) {
        *job_id = gg_obj_into_buf(*job_id_obj);
    }
    if (status != NULL) {
        *status = gg_obj_into_buf(*status_obj);
    }

    slot_pending = true;
    return GG_ERR_OK;
}

GgError status_keeper_clear(void) {
    GG_MTX_SCOPE_GUARD(&slot_mtx);

    // Self-gated no-op when nothing is pending (see status_keeper.h for the
    // startup-sync requirement).
    if (!slot_pending) {
        return GG_ERR_OK;
    }

    GgError ret = ggl_gg_config_delete(PENDING_STATUS_KEY_PATH);
    if (ret != GG_ERR_OK) {
        GG_LOGE(
            "Failed to clear pending deployment status: %s", gg_strerror(ret)
        );
        return ret;
    }

    slot_pending = false;
    GG_LOGD("Cleared pending deployment status slot.");
    return GG_ERR_OK;
}

bool status_keeper_has_pending(void) {
    GG_MTX_SCOPE_GUARD(&slot_mtx);
    return slot_pending;
}
