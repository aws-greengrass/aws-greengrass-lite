#ifndef GGHEALTHD_BUS_H
#define GGHEALTHD_BUS_H

#include <ggl/error.h>
#include <ggl/object.h>
GglError verify_component_exists(GglBuffer component_name);

GglError get_root_component_list(GglAlloc *alloc, GglList *component_list);

#endif
