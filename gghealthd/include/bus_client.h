#ifndef GGHEALTHD_BUS_H
#define GGHEALTHD_BUS_H

#include <ggl/alloc.h>
#include <ggl/buffer.h>
#include <ggl/error.h>
#include <ggl/object.h>

// use ggconfigd to verify a component's existence
GglError verify_component_exists(GglBuffer component_name);

// use ggconfigd to list root components
GglError get_root_component_list(GglAlloc *alloc, GglList *component_list);

#endif
