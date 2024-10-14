#ifndef GGL_STALE_COMPONENT_H
#define GGL_STALE_COMPONENT_H

#include <ggl/error.h>
#include <ggl/object.h>

GglError cleanup_stale_versions(GglMap latest_components_map);

#endif
