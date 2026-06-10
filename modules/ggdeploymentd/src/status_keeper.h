// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_STATUS_KEEPER_H
#define GGDEPLOYMENTD_STATUS_KEEPER_H

//! Single-slot persistence for the latest undelivered IoT Jobs status update.
//!
//! ggdeploymentd reports deployment status (IN_PROGRESS / SUCCEEDED / FAILED)
//! to IoT Jobs with a single fire-and-forget publish. If that publish fails
//! (flaky network, reboot mid-outage) the update is lost and the cloud-side job
//! is left stale. This module persists the pending status to ggconfigd so it
//! can be re-sent once connectivity returns or after a restart.
//!
//! greengrass lite only tracks one deployment at a time ($next), so a single
//! slot is stored at config `services/DeploymentService/pendingStatus` and
//! replaced in place -- this is intentionally NOT a queue.
//!
//! Pure storage: this module persists / reads / clears the slot. Flush
//! orchestration (deciding when to re-send) lives in iot_jobs_listener.c.
//!
//! Thread-safe: all access is guarded by an internal mutex.

#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>
#include <stdbool.h>

/// Persist (overwrite) the pending status slot.
///
/// Writes `{ job_id, status }` to the config slot, replacing any existing slot
/// in place. `job_id` is required to rebuild the update topic on re-send. On
/// success the in-memory "pending" hint is set.
GgError status_keeper_persist(GgBuffer job_id, GgBuffer status);

/// Read the pending status slot.
///
/// On success returns GG_ERR_OK and points `job_id` and `status` at buffers
/// allocated from `alloc` (valid for the lifetime of `alloc`'s backing memory).
/// Any out-pointer may be NULL if that field is not needed. Returns the
/// underlying config error if no slot is stored (typically GG_ERR_NOENTRY). On
/// success the in-memory "pending" hint is set, so calling this once at startup
/// syncs the hint with on-disk state.
GgError status_keeper_read(GgArena *alloc, GgBuffer *job_id, GgBuffer *status);

/// Clear the pending status slot.
///
/// Idempotent: if the in-memory "pending" hint indicates nothing is stored this
/// is a no-op that issues no config call (keeps the happy path free of I/O).
/// The hint must be synced first via status_keeper_read() at startup so a slot
/// persisted before a restart is not skipped. Clears the hint on success.
GgError status_keeper_clear(void);

/// In-memory check for whether a slot is (believed to be) pending.
///
/// Reads the cached hint (no config call of its own) to gate the periodic
/// flush retry. The config slot remains authoritative.
bool status_keeper_has_pending(void);

#endif
