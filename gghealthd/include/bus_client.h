#ifndef GGHEALTHD_BUS_H
#define GGHEALTHD_BUS_H

#include <ggl/error.h>
#include <ggl/object.h>
GglError get_component_version(GglBuffer component_name, GglBuffer *version);

GglError get_root_component_list(GglAlloc *alloc, GglList *component_list);

#endif
