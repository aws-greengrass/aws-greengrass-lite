#ifndef GGL_STALE_COMPONENT_H
#define GGL_STALE_COMPONENT_H

#include "deployment_model.h"
#include <gg/error.h>
#include <gg/types.h>

GgError disable_and_unlink_service(
    GgBuffer *component_name, PhaseSelection phase
);
GgError cleanup_stale_versions(GgMap latest_components_map);

#endif
