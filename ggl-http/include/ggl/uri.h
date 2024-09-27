#ifndef GGHTTPLIB_URI_H
#define GGHTTPLIB_URI_H

#include <ggl/alloc.h>
#include <ggl/error.h>
#include <ggl/object.h>

typedef struct GglUriInfo {
    GglBuffer scheme;
    GglBuffer host;
    GglBuffer path;
    GglBuffer file;
} GglUriInfo;

GglError gg_uri_parse(GglAlloc *alloc, GglBuffer uri, GglUriInfo *info);

#endif
