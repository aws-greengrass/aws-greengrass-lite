// aws-greengrass-lite - AWS IoT Greengrass runtime for constrained devices
// Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#ifndef GGDEPLOYMENTD_COMPONENT_CONFIG_H
#define GGDEPLOYMENTD_COMPONENT_CONFIG_H

#include "deployment_model.h"
#include <gg/arena.h>
#include <gg/error.h>
#include <gg/types.h>

GgError apply_configurations(
    GglDeployment *deployment, GgBuffer component_name, GgBuffer operation
);

/// Apply a single component's entry from the deployment's
/// componentToConfiguration map. Writes the merge payload into
/// services.<component_name>.configuration in ggconfigd.
GgError apply_component_to_configuration(
    GgBuffer component_name, GgMap component_to_configuration
);

/// Pure helper: parse a single componentToConfiguration entry value and
/// return pointers to the merge and/or reset payloads that should be
/// written to services.<component>.configuration.
///
/// Accepts both shapes:
///   - GG_TYPE_MAP: direct merge map (Rust SDK / core-bus form). No
///                  merge/reset wrapper — whole map is the merge payload.
///   - GG_TYPE_BUF: JSON string with optional `{"merge": {...}}` and
///                  optional `{"reset": [...]}` keys (CLI form).
///
/// Any other type returns GG_ERR_INVALID.
///
/// On GG_ERR_OK:
///   - *merge_out is either a valid pointer or NULL. NULL means "no merge
///     payload" — callers should skip writing.
///   - *reset_out (if non-NULL) is either a valid pointer or NULL. NULL
///     means "no reset payload" — callers should skip the reset step.
///
/// reset_out MAY be NULL. If NULL, reset extraction is skipped entirely —
/// useful for callers that only support merge (e.g. unit tests).
///
/// `alloc` is used to back the decoded JSON tree for the GG_TYPE_BUF path
/// and MUST outlive any use of *merge_out or *reset_out.
GgError extract_merge_and_reset_payloads(
    GgObject *config_update_obj,
    GgArena *alloc,
    GgObject **merge_out,
    GgObject **reset_out
);

#endif
