#ifndef GGL_STALE_COMPONENT_H
#define GGL_STALE_COMPONENT_H

#include "deployment_model.h"
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

GglError disable_and_unlink_service(
    GglBuffer *component_name, PhaseSelection phase
);
GglError cleanup_stale_versions(GglMap latest_components_map);

#endif
