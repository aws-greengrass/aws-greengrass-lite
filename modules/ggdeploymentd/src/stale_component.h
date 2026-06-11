#ifndef GGL_STALE_COMPONENT_H
#define GGL_STALE_COMPONENT_H

#include "deployment_model.h"
#include <gg/error.h>
#include <gg/types.h>
#include <gg/vector.h>

GgError disable_and_unlink_service(
    GgBuffer *component_name, PhaseSelection phase
);

/// Iterates installed component recipes and removes any whose name+version is
/// not in @p latest_components_map. Components whose name is missing entirely
/// from the map are treated as fully removed: their config tree, services and
/// recipe files are deleted.
///
/// If both @p removed_names_storage and @p removed_components_out are
/// non-NULL, fully-removed component names are also recorded in the output
/// (deduplicated). Each name is copied into @p removed_names_storage and a
/// slice referencing those bytes is pushed onto @p removed_components_out.
/// The slices remain valid as long as @p removed_names_storage's backing
/// buffer is alive.
GgError cleanup_stale_versions(
    GgMap latest_components_map,
    GgByteVec *removed_names_storage,
    GgBufVec *removed_components_out
);

#endif
